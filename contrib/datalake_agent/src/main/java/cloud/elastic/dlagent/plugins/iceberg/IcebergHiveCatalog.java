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

import cloud.elastic.dlagent.api.security.SecureLogin;
import cloud.elastic.dlagent.plugins.iceberg.utilities.IcebergUtilities;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.hive.conf.HiveConf;
import org.apache.iceberg.CatalogProperties;
import org.apache.iceberg.PartitionSpec;
import org.apache.iceberg.Schema;
import org.apache.iceberg.Table;
import org.apache.iceberg.catalog.TableIdentifier;
import org.apache.iceberg.hadoop.ConfigProperties;
import org.apache.iceberg.hive.DlIcebergHiveCatalog;

import com.google.common.base.Preconditions;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;

/**
 * Implementation of IcebergCatalog for tables stored in HiveCatalog.
 */
public class IcebergHiveCatalog implements IcebergCatalog {

    private static final Logger LOG = LoggerFactory.getLogger(IcebergHiveCatalog.class);

    private DlIcebergHiveCatalog hiveCatalog;
    private IcebergUtilities icebergUtilities;
    private Configuration configuration;

    public IcebergHiveCatalog(String catalogLocation,
                              IcebergUtilities icebergUtilities,
                              Configuration configuration,
                              SecureLogin secureLogin,
                              String serverName,
                              String configFile) {
        this.icebergUtilities = icebergUtilities;
        this.configuration = configuration;

        HiveConf conf = new HiveConf(configuration, IcebergHiveCatalog.class);
        conf.setBoolean(ConfigProperties.ENGINE_HIVE_ENABLED, true);

        hiveCatalog = new DlIcebergHiveCatalog(secureLogin, serverName, configFile);
        hiveCatalog.setConf(conf);

        Map<String, String> properties = icebergUtilities.composeCatalogProperties(this.configuration);
        if (catalogLocation != null) {
            properties.put(CatalogProperties.WAREHOUSE_LOCATION, catalogLocation);
        }

        properties.put(CatalogProperties.CLIENT_POOL_SIZE, "5");

        hiveCatalog.initialize("IcebergHiveCatalog", properties);
    }

    @Override
    public Table createTable(
            TableIdentifier identifier,
            Schema schema,
            PartitionSpec spec,
            String location,
            Map<String, String> properties) {
        return hiveCatalog.createTable(identifier, schema, spec, location, properties);
    }

    @Override
    public Table loadTable(String tableName) throws Exception {
        TableIdentifier tableId = icebergUtilities.getIcebergTableIdentifier(tableName);
        return loadTable(tableId, null, null);
    }

    @Override
    public Table loadTable(TableIdentifier tableId, String tableLocation,
                           Map<String, String> properties) throws Exception {
        Preconditions.checkState(tableId != null);
        return hiveCatalog.loadTable(tableId);
    }

    @Override
    public boolean dropTable(String tableName, boolean purge) {
        throw new UnsupportedOperationException("Iceberg accessor does not support dropTable operation.");
    }

    @Override
    public void renameTable(String tableName, String newTableName) {
        throw new UnsupportedOperationException("Iceberg accessor does not support renameTable operation.");
    }
}