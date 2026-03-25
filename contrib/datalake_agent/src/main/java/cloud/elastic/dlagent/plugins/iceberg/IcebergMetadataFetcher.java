/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

package cloud.elastic.dlagent.plugins.iceberg;

import cloud.elastic.dlagent.api.error.UnsupportedTypeException;
import cloud.elastic.dlagent.api.filter.Operator;
import cloud.elastic.dlagent.api.filter.TreeTraverser;
import cloud.elastic.dlagent.api.filter.FilterParser;
import cloud.elastic.dlagent.api.filter.Node;
import cloud.elastic.dlagent.api.filter.SupportedDataTypePruner;
import cloud.elastic.dlagent.api.filter.SupportedOperatorPruner;
import cloud.elastic.dlagent.api.io.DataType;
import cloud.elastic.dlagent.api.model.BasePlugin;
import cloud.elastic.dlagent.api.model.MetadataFetcher;
import cloud.elastic.dlagent.api.model.ScanTask;
import cloud.elastic.dlagent.api.model.CombinedTask;
import cloud.elastic.dlagent.api.model.Partition;
import cloud.elastic.dlagent.api.model.Metadata;
import cloud.elastic.dlagent.api.model.Fragment;
import cloud.elastic.dlagent.api.model.FragmentDescription;
import cloud.elastic.dlagent.api.utilities.ColumnDescriptor;
import cloud.elastic.dlagent.api.utilities.GpdbFragmentMetadata;
import cloud.elastic.dlagent.api.utilities.SpringContext;
import cloud.elastic.dlagent.api.utilities.Utilities;
import cloud.elastic.dlagent.plugins.hudi.utilities.FilePathUtils;
import cloud.elastic.dlagent.plugins.iceberg.utilities.IcebergUtilities;
import cloud.elastic.dlagent.service.rest.FileListRequest;
import org.apache.iceberg.Table;
import org.apache.iceberg.TableScan;
import org.apache.iceberg.catalog.TableIdentifier;
import org.apache.iceberg.AppendFiles;
import org.apache.iceberg.CombinedScanTask;
import org.apache.iceberg.TableProperties;
import org.apache.iceberg.Schema;
import org.apache.iceberg.Snapshot;
import org.apache.iceberg.DeleteFile;
import org.apache.iceberg.DataFile;
import org.apache.iceberg.FileScanTask;
import org.apache.iceberg.RowDelta;
import org.apache.iceberg.expressions.Expression;
import org.apache.iceberg.io.CloseableIterable;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.common.collect.Sets;
import org.apache.iceberg.types.TypeUtil;
import org.apache.iceberg.types.Types;
import org.apache.iceberg.exceptions.NoSuchTableException;

import java.io.IOException;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import static org.apache.iceberg.FileContent.EQUALITY_DELETES;

public class IcebergMetadataFetcher extends BasePlugin implements MetadataFetcher {
    private static final Logger LOG = LoggerFactory.getLogger(IcebergMetadataFetcher.class);

    // ----- members for predicate pushdown handling -----
    static final EnumSet<Operator> SUPPORTED_OPERATORS =
            EnumSet.of(
                    Operator.NOOP,
                    Operator.LESS_THAN,
                    Operator.GREATER_THAN,
                    Operator.LESS_THAN_OR_EQUAL,
                    Operator.GREATER_THAN_OR_EQUAL,
                    Operator.EQUALS,
                    Operator.NOT_EQUALS,
                    // Operator.LIKE,
                    Operator.IS_NULL,
                    Operator.IS_NOT_NULL,
                    Operator.IN,
                    Operator.OR,
                    Operator.AND,
                    Operator.NOT
            );

    static final EnumSet<DataType> SUPPORTED_DATATYPES =
            EnumSet.of(
                    DataType.BIGINT,
                    DataType.INTEGER,
                    DataType.SMALLINT,
                    DataType.REAL,
                    DataType.NUMERIC,
                    DataType.FLOAT8,
                    DataType.TEXT,
                    DataType.VARCHAR,
                    DataType.BPCHAR,
                    DataType.BOOLEAN,
                    DataType.DATE,
                    DataType.TIMESTAMP,
                    DataType.TIMESTAMP_WITH_TIME_ZONE,
                    DataType.TIME,
                    DataType.BYTEA
            );

    private final IcebergCatalogWrapper icebergClientWrapper;
    protected final IcebergUtilities icebergUtilities;

    public IcebergMetadataFetcher() {
        this(SpringContext.getBean(IcebergUtilities.class), SpringContext.getBean(IcebergCatalogWrapper.class));
    }

    IcebergMetadataFetcher(IcebergUtilities icebergUtilities, IcebergCatalogWrapper icebergClientWrapper) {
        this.icebergUtilities = icebergUtilities;
        this.icebergClientWrapper = icebergClientWrapper;
    }

    @Override
    public void afterPropertiesSet() {
        super.afterPropertiesSet();
    }

    public FragmentDescription getFragments(String pattern) throws Exception {
        IcebergCatalog catalog = icebergClientWrapper.getIcebergCatalog(context);
        Table table;
        try {
            table = catalog.loadTable(context.getDataSource());
        } catch (NoSuchTableException e) {
            return new FragmentDescription(null, java.util.Collections.emptyList(), -1L);
        }

        /* Get current snapshot ID (cheap: reads already-loaded metadata) */
        Snapshot currentSnapshot = table.currentSnapshot();
        long snapshotId = (currentSnapshot != null) ? currentSnapshot.snapshotId() : -1;

        /* Check conditional cache: if client has same snapshot, skip planTasks */
        String ifSnapshotStr = context.getOptions().get("if-snapshot-id");
        if (ifSnapshotStr != null) {
            try {
                long ifSnapshotId = Long.parseLong(ifSnapshotStr);
                if (snapshotId == ifSnapshotId) {
                    return FragmentDescription.notModified(snapshotId);
                }
            } catch (NumberFormatException e) {
                /* ignore malformed header, proceed normally */
            }
        }

        icebergUtilities.verifySchema(table.schema(), context);

        TableScan scan = table
                .newScan()
                .project(expectedSchema(table));

        Expression expression = filterExpression();
        if (expression != null) {
            scan = scan.filter(expression);
        }

        scan = scan.option(TableProperties.SPLIT_SIZE, String.valueOf(context.getSplitSize()));

        List<CombinedScanTask> scanTasks;
        try (CloseableIterable<CombinedScanTask> tasksIterable = scan.planTasks()) {
            scanTasks = Lists.newArrayList(tasksIterable);
        } catch (IOException e) {
            throw e;
        }

        return transformTasks(table, scanTasks, snapshotId);
    }

    public List<Partition> getPartitions(String pattern) throws Exception {
        throw new UnsupportedOperationException("Iceberg accessor does not support getPartitions operation.");
    }

    public Metadata getSchema(String pattern) throws Exception {
        Metadata.Item tblDesc = Utilities.extractTableFromName(context.getDataSource());
        Metadata metadata = new Metadata(tblDesc);

        IcebergCatalog catalog = icebergClientWrapper.getIcebergCatalog(context);
        Table table = catalog.loadTable(context.getDataSource());

        LOG.info("table properties {}", table.properties());

        try {
            for (Types.NestedField icebergCol : table.schema().columns()) {
                metadata.addField(icebergUtilities.mapIcebergType(icebergCol));
            }
        } catch (UnsupportedTypeException e) {
            String errorMsg = "Failed to retrieve metadata for table " + metadata.getItem() + ". " +
                    e.getMessage();
            throw new UnsupportedTypeException(errorMsg);
        }

        return metadata;
    }

    @Override
    public Boolean batchAppend() throws Exception {
        Table table = null;
        IcebergCatalog catalog = icebergClientWrapper.getIcebergCatalog(context);
        try {
            table = catalog.loadTable(context.getDataSource());
        } catch (NoSuchTableException e) {
            Map<String, String> properties = new HashMap<String, String>();
            properties.put(TableProperties.FORMAT_VERSION, "2");
            properties.put(TableProperties.UPDATE_MODE, "merge-on-read");
            properties.put(TableProperties.DELETE_MODE, "merge-on-read");
            properties.put(TableProperties.MERGE_MODE, "merge-on-read");

            try {
                table = catalog.createTable(TableIdentifier.parse(context.getDataSource()),
                                            icebergUtilities.formSchemaFromTupleDes(context),
                                            null,
                                            null,
                                            properties);
            } catch (org.apache.iceberg.exceptions.AlreadyExistsException ae) {
                // Another segment created the table concurrently, just load it
                table = catalog.loadTable(context.getDataSource());
            }
        }
        AppendFiles batchAppend = table.newAppend();

        // Use file list from JSON POST request body
        for (FileListRequest.FileEntry fileEntry : context.getFileList()) {
            DataFile dataFile = icebergUtilities.transFileFromFileEntry(fileEntry);
            batchAppend.appendFile(dataFile);
        }

        batchAppend.commit();
        return true;
    }

    @Override
    public Metadata getOrCreateSchema() throws Exception {
        Metadata.Item tblDesc = Utilities.extractTableFromName(context.getDataSource());
        Metadata metadata = new Metadata(tblDesc);
        Table table = null;
        IcebergCatalog catalog = icebergClientWrapper.getIcebergCatalog(context);
        try {
            table = catalog.loadTable(context.getDataSource());
        } catch (NoSuchTableException e) {
            table = catalog.createTable(TableIdentifier.parse(context.getDataSource()),
                                        icebergUtilities.formSchemaFromTupleDes(context),
                                        null,
                                        null,
                                        java.util.Collections.singletonMap(TableProperties.FORMAT_VERSION, "2"));
        }
        try {
            for (Types.NestedField icebergCol : table.schema().columns()) {
                metadata.addField(icebergUtilities.mapIcebergType(icebergCol));
            }
            metadata.setLocation(table.location());
        } catch (UnsupportedTypeException e) {
            String errorMsg = "Failed to retrieve metadata for table " + metadata.getItem() + ". " +
                    e.getMessage();
            throw new UnsupportedTypeException(errorMsg);
        }
        return metadata;
    }

    @Override
    public Boolean rowUpdate() throws Exception {
        IcebergCatalog catalog = icebergClientWrapper.getIcebergCatalog(context);
        Table table = catalog.loadTable(context.getDataSource());
        RowDelta rowDelta = table.newRowDelta();

        // Use file list from JSON POST request body
        for (FileListRequest.FileEntry fileEntry : context.getFileList()) {
            String content = fileEntry.getContent();
            if ("POSITION_DELETE".equals(content)) {
                DeleteFile deleteFile = icebergUtilities.transPosDeleteFromFileEntry(fileEntry);
                rowDelta.addDeletes(deleteFile);
            } else {
                // Default to DATA_FILE
                DataFile dataFile = icebergUtilities.transFileFromFileEntry(fileEntry);
                rowDelta.addRows(dataFile);
            }
        }

        rowDelta.commit();
        return true;
    }

    @Override
    public Map<String, String> getCurrentSnapshotSummary() throws Exception {
        IcebergCatalog catalog = icebergClientWrapper.getIcebergCatalog(context);
        Table table;
        try {
            table = catalog.loadTable(context.getDataSource());
        } catch (NoSuchTableException e) {
            return java.util.Collections.emptyMap();
        }
        Snapshot currentSnapshot = table.currentSnapshot();
        if (currentSnapshot == null) {
            return java.util.Collections.emptyMap();
        }
        return currentSnapshot.summary();
    }

    public Schema expectedSchema(Table table) {
        Map<String, Types.NestedField> columnMetadata = Maps.newHashMap();

        table.schema().columns().stream()
                .forEach(column -> columnMetadata.put(column.name(), column));

        List<Types.NestedField> projectedFields = context.getTupleDescription().stream()
                .filter(ColumnDescriptor::isProjected)
                .map(c -> {
                    Types.NestedField t = columnMetadata.get(c.columnName());
                    if (t == null) {
                        throw new IllegalArgumentException(
                                String.format("Column %s is missing from iceberg schema", c.columnName()));
                    }
                    return t;
                })
                .collect(Collectors.toList());
        return new Schema(projectedFields);
    }

    protected EnumSet<DataType> getSupportedDatatypesForPushdown() {
        return SUPPORTED_DATATYPES;
    }

    protected EnumSet<Operator> getSupportedOperatorsForPushdown() {
        return SUPPORTED_OPERATORS;
    }

    protected Expression filterExpression() throws Exception {
        if (!context.hasFilter()) {
            return null;
        }

        /* Predicate push-down configuration */
        IcebergExpressionBuilder expressionBuilder =
                new IcebergExpressionBuilder(context.getTupleDescription());

        // Parse the filter string into a expression tree Node
        Node root = new FilterParser().parse(FilePathUtils.unescapeString(context.getFilterString()));
        TreeTraverser traverser = new TreeTraverser();

        // Prune the parsed tree with valid supported datatypes and operators and then
        // traverse the pruned tree with the expressionBuilder to produce a Expression
        traverser.traverse(
                root,
                new SupportedDataTypePruner(context.getTupleDescription(), getSupportedDatatypesForPushdown()),
                new SupportedOperatorPruner(getSupportedOperatorsForPushdown()),
                expressionBuilder);

        Expression expression = expressionBuilder.build();
        if (expression == null) {
            return null;
        }

        LOG.debug("filter expression {}", expression.toString());

        return expression;
    }

    private List<String> getEqColumnNames(Table table, DeleteFile delete) {
        Set<Integer> deleteIds = Sets.newHashSet(delete.equalityFieldIds());
        Schema deleteSchema = TypeUtil.select(table.schema(), deleteIds);

        List<String> eqColumnNames = deleteSchema.columns().stream()
                .map(Types.NestedField::name)
                .collect(Collectors.toList());

        return eqColumnNames;
    }

    private FragmentDescription transformTasks(Table table, List<CombinedScanTask> scanTasks, long snapshotId) {
        // Pass 1: collect all unique delete files by path
        Map<String, Fragment> uniqueDeleteMap = new LinkedHashMap<>();
        for (CombinedScanTask combinedScanTask : scanTasks) {
            for (FileScanTask fileScanTask : combinedScanTask.tasks()) {
                if (fileScanTask.isDataTask()) {
                    continue;
                }
                for (DeleteFile delete : fileScanTask.deletes()) {
                    String path = delete.path().toString();
                    if (!uniqueDeleteMap.containsKey(path)) {
                        List<String> deleteSchemas = null;
                        if (delete.content() == EQUALITY_DELETES) {
                            deleteSchemas = getEqColumnNames(table, delete);
                        }
                        uniqueDeleteMap.put(path, new Fragment(path,
                                new IcebergFileFragmentMetadata(delete.format(), delete.content(), delete.recordCount(), deleteSchemas)));
                    }
                }
            }
        }

        // Build index lookup: path -> index in deleteFiles list
        List<Fragment> allDeleteFiles = new ArrayList<>(uniqueDeleteMap.values());
        Map<String, Integer> deleteIndexMap = new HashMap<>();
        for (int i = 0; i < allDeleteFiles.size(); i++) {
            deleteIndexMap.put(allDeleteFiles.get(i).getSourceName(), i);
        }

        // Pass 2: build tasks with delete index references
        List<CombinedTask> tasks = Lists.newArrayList();
        for (CombinedScanTask combinedScanTask : scanTasks) {
            List<ScanTask> combinedTask = Lists.newArrayList();
            for (FileScanTask fileScanTask : combinedScanTask.tasks()) {
                if (fileScanTask.isDataTask()) {
                    continue;
                }

                DataFile file = fileScanTask.file();
                Fragment data = new Fragment(file.path().toString(),
                        new IcebergFileFragmentMetadata(file.format(), file.content(), file.recordCount(), null));

                List<Integer> deleteIndexes = new ArrayList<>();
                for (DeleteFile delete : fileScanTask.deletes()) {
                    Integer deleteIdx = deleteIndexMap.get(delete.path().toString());
                    if (deleteIdx == null) {
                        LOG.warn("Delete file path {} not found in global index map, skipping", delete.path());
                        continue;
                    }
                    deleteIndexes.add(deleteIdx);
                }

                combinedTask.add(new ScanTask(data, deleteIndexes, fileScanTask.start(), fileScanTask.length()));
            }

            tasks.add(new CombinedTask(combinedTask));
        }

        LOG.info("transformTasks: {} unique delete files, {} combined tasks",
                allDeleteFiles.size(), tasks.size());

        return new FragmentDescription(null, allDeleteFiles, tasks, snapshotId);
    }

    public boolean open() throws Exception {
        return true;
    }

    public void close() throws Exception {
    }
}
