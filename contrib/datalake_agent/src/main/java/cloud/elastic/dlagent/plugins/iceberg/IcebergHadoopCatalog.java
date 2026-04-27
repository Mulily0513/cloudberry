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

import java.io.UncheckedIOException;
import java.util.Map;
import java.util.List;

import cloud.elastic.dlagent.plugins.iceberg.utilities.IcebergUtilities;
import org.apache.hadoop.conf.Configuration;
import org.apache.iceberg.PartitionSpec;
import org.apache.iceberg.CatalogProperties;
import org.apache.iceberg.Schema;
import org.apache.iceberg.Table;
import org.apache.iceberg.catalog.TableIdentifier;
import org.apache.iceberg.hadoop.HadoopCatalog;
import org.apache.iceberg.exceptions.NoSuchTableException;
import com.google.common.base.Preconditions;
import org.apache.iceberg.avro.Avro;


import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Implementation of IcebergCatalog for tables handled by Iceberg's Catalogs API.
 */
public class IcebergHadoopCatalog implements IcebergCatalog {

    private static final Logger LOG = LoggerFactory.getLogger(IcebergHadoopCatalog.class);

    private HadoopCatalog hadoopCatalog;
    private IcebergUtilities icebergUtilities;
    private Configuration configuration;
    private boolean isUseGopherClient = true;

    public void createDefaultHadoopCatalog(String catalogLocation, IcebergUtilities icebergUtilities, Configuration configuration) {
        this.icebergUtilities = icebergUtilities;
        this.configuration = configuration;
        this.hadoopCatalog = new HadoopCatalog();

        // Hadoop's S3AFileSystem stages object uploads through hadoop.tmp.dir
        // and HDFS clients use it for local intermediate buffers.
        if (configuration.get("hadoop.tmp.dir") == null) {
            configuration.set("hadoop.tmp.dir",
                "/tmp/hadoop-" + System.getProperty("user.name"));
        }

        Map<String, String> props = icebergUtilities.composeCatalogProperties(this.configuration);

        // Use gopher:// URI when Gopher is enabled, otherwise use standard hdfs:// URI for HadoopFileIO
        String warehouseLocation;
        if (icebergUtilities.isGopherEnabled(this.configuration)) {
            warehouseLocation = convertToGopherURI(configuration.get("fs.defaultFS"), catalogLocation);
        } else {
            warehouseLocation = buildStandardURI(configuration.get("fs.defaultFS"), catalogLocation);
        }
        props.put(CatalogProperties.WAREHOUSE_LOCATION, warehouseLocation);

        LOG.info("warehouse location of iceberg hadoop-table {}", warehouseLocation);

        hadoopCatalog.setConf(this.configuration);
        hadoopCatalog.initialize("", props);
    }

    /**
     * Builds a standard hdfs:// URI for use with HadoopFileIO (non-Gopher mode).
     */
    private String buildStandardURI(String defaultFS, String catalogLocation) {
        if (defaultFS != null && !defaultFS.isEmpty()) {
            if (catalogLocation.startsWith("/")) {
                return defaultFS + catalogLocation;
            }
            return defaultFS + "/" + catalogLocation;
        }
        return catalogLocation;
    }

    /**
     * Converts legacy URI to gopher:// format.
     *
     * <p>Examples:
     * <ul>
     * <li>hdfs://namenode:9000/warehouse → gopher://warehouse</li>
     * <li>s3a://bucket/path → gopher://bucket/path</li>
     * <li>oss://bucket/path → gopher://bucket/path</li>
     * </ul>
     */
    private String convertToGopherURI(String defaultFS, String catalogLocation) {
        if (defaultFS == null || defaultFS.isEmpty()) {
            // If no defaultFS, assume gopher format directly
            return "gopher://" + catalogLocation;
        }

        // Parse the defaultFS to extract scheme and authority
        // For HDFS: hdfs://namenode:port -> use gopher with namenode config
        // For S3/OSS: s3a://bucket or oss://bucket -> use gopher://bucket
        if (defaultFS.startsWith("hdfs://")) {
            // HDFS: use gopher://hdfs<path> to avoid URI normalization issues
            // GopherFileSystem is registered as fs.gopher.impl
            return "gopher://hdfs" + (catalogLocation.startsWith("/") ? catalogLocation : "/" + catalogLocation);
        } else if (defaultFS.startsWith("s3a://") || defaultFS.startsWith("s3://") ||
                   defaultFS.startsWith("oss://")) {
            // Object storage: extract bucket from defaultFS
            int schemeEnd = defaultFS.indexOf("://");
            String afterScheme = defaultFS.substring(schemeEnd + 3);
            int slashPos = afterScheme.indexOf('/');
            String bucket = (slashPos > 0) ? afterScheme.substring(0, slashPos) : afterScheme;

            // Construct gopher URI
            if (catalogLocation.startsWith("/")) {
                return "gopher://" + bucket + catalogLocation;
            } else {
                return "gopher://" + bucket + "/" + catalogLocation;
            }
        } else {
            // Unknown scheme, default to gopher
            LOG.warn("Unknown defaultFS scheme: {}, using gopher://", defaultFS);
            return "gopher://" + catalogLocation;
        }
    }

    public void createGopherHadoopCatalog(String catalogLocation,
                                          IcebergUtilities icebergUtilities,
                                          Configuration configuration,
                                          Map<String, String> gopherProperties) {
        this.icebergUtilities = icebergUtilities;
        this.configuration = configuration;
        this.hadoopCatalog = new HadoopCatalog();

        // See createDefaultHadoopCatalog for rationale.
        if (configuration.get("hadoop.tmp.dir") == null) {
            configuration.set("hadoop.tmp.dir",
                "/tmp/hadoop-" + System.getProperty("user.name"));
        }

        Map<String, String> props = icebergUtilities.composeCatalogProperties(this.configuration);

        // Use gopher:// URI when Gopher is enabled, otherwise use standard hdfs:// URI for HadoopFileIO
        String warehouseLocation;
        if (icebergUtilities.isGopherEnabled(this.configuration)) {
            warehouseLocation = convertToGopherURI(configuration.get("fs.defaultFS"), catalogLocation);
        } else {
            warehouseLocation = buildStandardURI(configuration.get("fs.defaultFS"), catalogLocation);
        }
        props.put(CatalogProperties.WAREHOUSE_LOCATION, warehouseLocation);


        if (gopherProperties != null) {
            for (Map.Entry<String, String> entry : gopherProperties.entrySet()) {
                props.put(entry.getKey(), entry.getValue());
            }
        }
        //TODO(liuxiaoyu): need set gopherFileIO
        //props.put(CatalogProperties.FILE_IO_IMPL, "");

        LOG.debug("iceberg hadoop catalog gopher properties: {}", gopherProperties);
        LOG.info("warehouse location of iceberg hadoop-table {}", warehouseLocation);

        hadoopCatalog.setConf(this.configuration);
        hadoopCatalog.initialize("", props);
    }

    public IcebergHadoopCatalog(String catalogLocation,
                                IcebergUtilities icebergUtilities,
                                Configuration configuration,
                                Map<String, String> gopherProperties) {
        if (isUseGopherClient) {
            createGopherHadoopCatalog(catalogLocation, icebergUtilities, configuration, gopherProperties);
        } else {
            createDefaultHadoopCatalog(catalogLocation, icebergUtilities, configuration);
        }
    }

    @Override
    public Table createTable(
            TableIdentifier identifier,
            Schema schema,
            PartitionSpec spec,
            String location,
            Map<String, String> tableProps) throws Exception {
        // Iceberg's path-based HadoopCatalog rejects any non-null custom
        // location (it enforces <warehouse>/<ns>/<table>). The volume URL
        // forwarded from IcebergRestController.createTable would trip that
        // check; pass null and let iceberg compute the standard layout from
        // CatalogProperties.WAREHOUSE_LOCATION (set in initialize via the
        // fs.defaultFS + catalogLocation contract).
        return hadoopCatalog.createTable(identifier, schema, spec, null,
                IcebergUtilities.stripInternalProperties(tableProps));
    }

    @Override
    public Table loadTable(String tableName) throws Exception {
        TableIdentifier tableId = icebergUtilities.getIcebergTableIdentifier(tableName);
        return loadTable(tableId, null, null);
    }

    @Override
    public Table loadTable(TableIdentifier tableId, String tableLocation,
                           Map<String, String> tableProps) throws Exception {
        Preconditions.checkState(tableId != null);
        final int MAX_ATTEMPTS = 5;
        final int SLEEP_MS = 500;
        int attempt = 0;
        while (attempt < MAX_ATTEMPTS) {
            try {
                return hadoopCatalog.loadTable(tableId);
            } catch (NullPointerException | UncheckedIOException e) {
                if (attempt == MAX_ATTEMPTS - 1) {
                    // Throw exception on last attempt.
                    throw e;
                }
                LOG.warn("Caught Exception during Iceberg table loading: {}: {}", tableId, e);
            }
            ++attempt;
            try {
                Thread.sleep(SLEEP_MS);
            } catch (InterruptedException e) {
                // Ignored.
            }
        }
        // We shouldn't really get there, but to make the compiler happy:
        throw new Exception(
                String.format("Failed to load Iceberg table with id: %s", tableId));
    }

    @Override
    public boolean dropTable(String tableName, boolean purge) {
        throw new UnsupportedOperationException("Iceberg accessor does not support dropTable operation.");
    }

    @Override
    public void renameTable(String tableName, String newTableName) {
        throw new UnsupportedOperationException("Iceberg accessor does not support renameTable operation.");
    }

    @Override
    public boolean createNamespace(String catalogName, String namespaceName, 
                                  Map<String, String> properties) throws Exception {
        throw new UnsupportedOperationException("Hadoop catalog does not support createNamespace operation.");
    }
}

