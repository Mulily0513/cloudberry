#pragma once
#include "agent_cjson_builder.hpp"
#include <string>
#include <chrono>
#include <sstream>

class TestUtils {
public:
    // Generate unique table name with timestamp
    static std::string generateUniqueTableName(const std::string& prefix = "test_table") {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::ostringstream oss;
        oss << prefix << "_" << timestamp;
        return oss.str();
    }

    // Create IcebergCatalogConfig
    static agentcli_cJSON* createCatalogConfig() {
        agentcli_cJSON *catalogConfig = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(catalogConfig, "server_type", "hive");
        agentcli_cJSON_AddStringToObject(catalogConfig, "hive_metastore_uri", "thrift://hive-metastore:9083");
        agentcli_cJSON_AddStringToObject(catalogConfig, "auth_method", "simple");
        agentcli_cJSON_AddStringToObject(catalogConfig, "warehouse_location_prefix", "s3a://warehouse/hive_location");
        return catalogConfig;
    }

    // Create IcebergVolumeConfig
    static agentcli_cJSON* createVolumeConfig() {
        agentcli_cJSON *volumeConfig = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(volumeConfig, "volume_server_type", "s3a");
        agentcli_cJSON_AddStringToObject(volumeConfig, "volume_endpoint", "http://minio:9000");
        agentcli_cJSON_AddStringToObject(volumeConfig, "volume_region", "us-east-1");
        agentcli_cJSON_AddStringToObject(volumeConfig, "bucket_name", "warehouse");
        agentcli_cJSON_AddBoolToObject(volumeConfig, "path_style_access", 1);
        agentcli_cJSON_AddStringToObject(volumeConfig, "access_key_id", "admin");
        agentcli_cJSON_AddStringToObject(volumeConfig, "secret_access_key", "password");
        return volumeConfig;
    }

    // Create IcebergAdditionalConfig
    static agentcli_cJSON* createAdditionalConfig() {
        agentcli_cJSON *additionalConfig = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(additionalConfig, "totalSegment", "1");
        agentcli_cJSON_AddStringToObject(additionalConfig, "splitSize", "128");
        agentcli_cJSON_AddStringToObject(additionalConfig, "filterString", "");
        return additionalConfig;
    }

    // Create complete IcebergConfig
    static agentcli_cJSON* createIcebergConfig() {
        agentcli_cJSON *icebergConfig = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(icebergConfig, "IcebergCatalogConfig", createCatalogConfig());
        agentcli_cJSON_AddItemToObject(icebergConfig, "IcebergVolumeConfig", createVolumeConfig());
        agentcli_cJSON_AddItemToObject(icebergConfig, "IcebergAdditionalConfig", createAdditionalConfig());
        return icebergConfig;
    }

    // Create table schema
    static agentcli_cJSON* createTableSchema() {
        agentcli_cJSON *schema = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(schema, "type", "struct");
        agentcli_cJSON_AddNumberToObject(schema, "schema-id", 0);

        agentcli_cJSON *fields = agentcli_cJSON_CreateArray();

        // ID field
        agentcli_cJSON *idField = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(idField, "id", 1);
        agentcli_cJSON_AddStringToObject(idField, "name", "id");
        agentcli_cJSON_AddStringToObject(idField, "type", "long");
        agentcli_cJSON_AddBoolToObject(idField, "required", 1);
        agentcli_cJSON_AddItemToArray(fields, idField);

        // Name field
        agentcli_cJSON *nameField = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(nameField, "id", 2);
        agentcli_cJSON_AddStringToObject(nameField, "name", "name");
        agentcli_cJSON_AddStringToObject(nameField, "type", "string");
        agentcli_cJSON_AddBoolToObject(nameField, "required", 1);
        agentcli_cJSON_AddItemToArray(fields, nameField);

        // Age field
        agentcli_cJSON *ageField = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(ageField, "id", 3);
        agentcli_cJSON_AddStringToObject(ageField, "name", "age");
        agentcli_cJSON_AddStringToObject(ageField, "type", "int");
        agentcli_cJSON_AddBoolToObject(ageField, "required", 0);
        agentcli_cJSON_AddItemToArray(fields, ageField);

        agentcli_cJSON_AddItemToObject(schema, "fields", fields);
        return schema;
    }

    // Create test JSON configuration with custom table name
    static const char* createTestJsonConfig(const std::string& table_name) {
        agentcli_cJSON *request = agentcli_cJSON_CreateObject();

        agentcli_cJSON_AddItemToObject(request, "IcebergConfig", createIcebergConfig());
        agentcli_cJSON_AddItemToObject(request, "schema", createTableSchema());
        agentcli_cJSON_AddStringToObject(request, "name", table_name.c_str());

        char* json_string = agentcli_cJSON_PrintUnformatted(request);
        agentcli_cJSON_Delete(request);
        return json_string;
    }

    static agentcli_cJSON* createAppendJson(const std::string& table_name) {
        // Create fragments array
        agentcli_cJSON *fragments = agentcli_cJSON_CreateArray();

        // Fragment 1
        agentcli_cJSON *fragment1 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(fragment1, "path", ("/warehouse/" + table_name + "/data/file1.parquet").c_str());
        agentcli_cJSON_AddStringToObject(fragment1, "format", "PARQUET");
        agentcli_cJSON_AddNumberToObject(fragment1, "record_count", 100);
        agentcli_cJSON_AddNumberToObject(fragment1, "file_size_in_bytes", 10240);

        // Partition (empty object)
        agentcli_cJSON *partition1 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(fragment1, "partition", partition1);

        // Column sizes
        agentcli_cJSON *columnSizes1 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(columnSizes1, "1", 800);
        agentcli_cJSON_AddNumberToObject(columnSizes1, "2", 1200);
        agentcli_cJSON_AddNumberToObject(columnSizes1, "3", 400);
        agentcli_cJSON_AddItemToObject(fragment1, "column_sizes", columnSizes1);

        // Value counts
        agentcli_cJSON *valueCounts1 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(valueCounts1, "1", 100);
        agentcli_cJSON_AddNumberToObject(valueCounts1, "2", 100);
        agentcli_cJSON_AddNumberToObject(valueCounts1, "3", 95);
        agentcli_cJSON_AddItemToObject(fragment1, "value_counts", valueCounts1);

        // Null value counts
        agentcli_cJSON *nullCounts1 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(nullCounts1, "1", 0);
        agentcli_cJSON_AddNumberToObject(nullCounts1, "2", 0);
        agentcli_cJSON_AddNumberToObject(nullCounts1, "3", 5);
        agentcli_cJSON_AddItemToObject(fragment1, "null_value_counts", nullCounts1);

        agentcli_cJSON_AddItemToArray(fragments, fragment1);

        // Fragment 2
        agentcli_cJSON *fragment2 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(fragment2, "path", ("/warehouse/" + table_name + "/data/file2.parquet").c_str());
        agentcli_cJSON_AddStringToObject(fragment2, "format", "PARQUET");
        agentcli_cJSON_AddNumberToObject(fragment2, "record_count", 200);
        agentcli_cJSON_AddNumberToObject(fragment2, "file_size_in_bytes", 20480);

        // Partition (empty object)
        agentcli_cJSON *partition2 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(fragment2, "partition", partition2);

        // Column sizes
        agentcli_cJSON *columnSizes2 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(columnSizes2, "1", 1600);
        agentcli_cJSON_AddNumberToObject(columnSizes2, "2", 2400);
        agentcli_cJSON_AddNumberToObject(columnSizes2, "3", 800);
        agentcli_cJSON_AddItemToObject(fragment2, "column_sizes", columnSizes2);

        // Value counts
        agentcli_cJSON *valueCounts2 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(valueCounts2, "1", 200);
        agentcli_cJSON_AddNumberToObject(valueCounts2, "2", 200);
        agentcli_cJSON_AddNumberToObject(valueCounts2, "3", 190);
        agentcli_cJSON_AddItemToObject(fragment2, "value_counts", valueCounts2);

        // Null value counts
        agentcli_cJSON *nullCounts2 = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddNumberToObject(nullCounts2, "1", 0);
        agentcli_cJSON_AddNumberToObject(nullCounts2, "2", 0);
        agentcli_cJSON_AddNumberToObject(nullCounts2, "3", 10);
        agentcli_cJSON_AddItemToObject(fragment2, "null_value_counts", nullCounts2);

        agentcli_cJSON_AddItemToArray(fragments, fragment2);

        return fragments;
    }

    static const char* createTestAppendJsonConfig(const std::string& table_name) {
        agentcli_cJSON *request = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(request, "IcebergConfig", createIcebergConfig());
        agentcli_cJSON_AddItemToObject(request, "fragments", createAppendJson(table_name));

        char* json_string = agentcli_cJSON_PrintUnformatted(request);
        agentcli_cJSON_Delete(request);
        return json_string;
    }

    static agentcli_cJSON* createUpdateJson(const std::string& table_name) {
        agentcli_cJSON *fragments = agentcli_cJSON_CreateArray();

        agentcli_cJSON *deleteFragment = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(deleteFragment, "position_on_delete", "EQUALITY_DELETE");
        agentcli_cJSON_AddStringToObject(deleteFragment, "path", ("/warehouse/" + table_name + "/deletes/delete-all.parquet").c_str());
        agentcli_cJSON_AddStringToObject(deleteFragment, "format", "PARQUET");
        agentcli_cJSON_AddNumberToObject(deleteFragment, "record_count", 0);
        agentcli_cJSON_AddNumberToObject(deleteFragment, "file_size_in_bytes", 256);

        agentcli_cJSON_AddItemToArray(fragments, deleteFragment);
        return fragments;
    }

    static const char* createTestUpdateJsonConfig(const std::string& table_name) {
        agentcli_cJSON *request = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(request, "IcebergConfig", createIcebergConfig());
        agentcli_cJSON_AddItemToObject(request, "updateFragments", createUpdateJson(table_name));

        char* json_string = agentcli_cJSON_PrintUnformatted(request);
        agentcli_cJSON_Delete(request);
        return json_string;
    }

    static void print_formatted_response(const char* operation_name, const char* json_response) {
        printf("%s\n", operation_name);

        if (!json_response) return;

        agentcli_cJSON *json = agentcli_cJSON_Parse(json_response);
        if (!json) return;

        agentcli_cJSON *error = agentcli_cJSON_GetObjectItem(json, "error");
        if (error) {
            printf("=== ERROR RESPONSE ===\n");

            agentcli_cJSON *message = agentcli_cJSON_GetObjectItem(error, "message");
            if (message && agentcli_cJSON_IsString(message)) {
                printf("Message: %s\n", message->valuestring);
            }

            agentcli_cJSON *code = agentcli_cJSON_GetObjectItem(error, "code");
            if (code && agentcli_cJSON_IsNumber(code)) {
                printf("Code: %d\n", (int)code->valuedouble);
            }

            agentcli_cJSON *type = agentcli_cJSON_GetObjectItem(error, "type");
            if (type && agentcli_cJSON_IsString(type)) {
                printf("Type: %s\n", type->valuestring);
            }

            agentcli_cJSON *stack = agentcli_cJSON_GetObjectItem(error, "stack");
            if (stack && agentcli_cJSON_IsString(stack)) {
                printf("Stack Trace:\n");
                char *pos = stack->valuestring;
                while (*pos) {
                    if (pos[0] == '\\' && pos[1] == 'n') {
                        printf("\n");
                        pos += 2;
                    } else if (pos[0] == '\\' && pos[1] == 't') {
                        printf("\t");
                        pos += 2;
                    } else {
                        printf("%c", *pos);
                        pos++;
                    }
                }
                printf("\n");
            }
        } else {
            printf("=== SUCCESS RESPONSE ===\n");

            printf("%s\n", json_response);

            agentcli_cJSON *status = agentcli_cJSON_GetObjectItem(json, "status");
            if (status && agentcli_cJSON_IsString(status)) {
                printf("Status: %s\n", status->valuestring);
            }

            agentcli_cJSON *operation = agentcli_cJSON_GetObjectItem(json, "operation");
            if (operation && agentcli_cJSON_IsString(operation)) {
                printf("Operation: %s\n", operation->valuestring);
            }

            agentcli_cJSON *table = agentcli_cJSON_GetObjectItem(json, "table");
            if (table && agentcli_cJSON_IsString(table)) {
                printf("Table: %s\n", table->valuestring);
            }

            agentcli_cJSON *namespace_obj = agentcli_cJSON_GetObjectItem(json, "namespace");
            if (namespace_obj && agentcli_cJSON_IsString(namespace_obj)) {
                printf("Namespace: %s\n", namespace_obj->valuestring);
            }
        }

        agentcli_cJSON_Delete(json);
        printf("========================\n");
    }

    // Create Iceberg Polaris configuration
    static agentcli_cJSON* createIcebergPolarisConfig() {
        agentcli_cJSON *icebergConfig = agentcli_cJSON_CreateObject();
        agentcli_cJSON *catalogConfig = agentcli_cJSON_CreateObject();
        
        agentcli_cJSON_AddStringToObject(catalogConfig, "server_type", "polaris");
        agentcli_cJSON_AddStringToObject(catalogConfig, "polaris_server_url", "http://singlecluster-polaris-1:8181/api/catalog");
        agentcli_cJSON_AddStringToObject(catalogConfig, "client_id", "root");
        agentcli_cJSON_AddStringToObject(catalogConfig, "client_secret", "s3cr3t");
        agentcli_cJSON_AddStringToObject(catalogConfig, "scope", "PRINCIPAL_ROLE:ALL");
        
        agentcli_cJSON_AddItemToObject(icebergConfig, "IcebergCatalogConfig", catalogConfig);
        return icebergConfig;
    }

    // Create test JSON for catalog creation
    static std::string createTestCatalogJsonConfig(const std::string& catalog_name, const std::string& namespace_name) {
        agentcli_cJSON *request = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(request, "IcebergConfig", createIcebergPolarisConfig());

        // Create catalog object
        agentcli_cJSON *catalog = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(catalog, "name", catalog_name.c_str());
        agentcli_cJSON_AddStringToObject(catalog, "type", "INTERNAL");
        agentcli_cJSON_AddBoolToObject(catalog, "readOnly", false);

        // Add properties
        agentcli_cJSON *properties = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(properties, "default-base-location", "s3://warehouse");
        agentcli_cJSON_AddItemToObject(catalog, "properties", properties);

        // Add storageConfigInfo
        agentcli_cJSON *storageConfigInfo = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddStringToObject(storageConfigInfo, "storageType", "S3");

        agentcli_cJSON *allowedLocations = agentcli_cJSON_CreateArray();
        agentcli_cJSON_AddItemToArray(allowedLocations, agentcli_cJSON_CreateString("s3://warehouse"));
        agentcli_cJSON_AddItemToObject(storageConfigInfo, "allowedLocations", allowedLocations);

        agentcli_cJSON_AddStringToObject(storageConfigInfo, "endpoint", "http://lakehouse:9100");
        agentcli_cJSON_AddStringToObject(storageConfigInfo, "endpointInternal", "http://lakehouse:9100");
        agentcli_cJSON_AddBoolToObject(storageConfigInfo, "pathStyleAccess", true);
        agentcli_cJSON_AddItemToObject(catalog, "storageConfigInfo", storageConfigInfo);

        agentcli_cJSON_AddItemToObject(request, "catalog", catalog);

        char *json_string = agentcli_cJSON_Print(request);
        std::string result(json_string);
        free(json_string);
        agentcli_cJSON_Delete(request);
        return result;
    }

    // Create test JSON for namespace creation
    static std::string createTestNamespaceJsonConfig(const std::string& catalog_name, const std::string& namespace_name) {
        agentcli_cJSON *request = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(request, "IcebergConfig", createIcebergPolarisConfig());
        
        // Add catalogName and namespaceName as required by CreateNamespaceRequest schema
        agentcli_cJSON_AddStringToObject(request, "catalogName", catalog_name.c_str());
        agentcli_cJSON_AddStringToObject(request, "namespaceName", namespace_name.c_str());
        
        // Add namespaceProperties object
        agentcli_cJSON *namespaceProperties = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(request, "namespaceProperties", namespaceProperties);

        char *json_string = agentcli_cJSON_Print(request);
        std::string result(json_string);
        free(json_string);
        agentcli_cJSON_Delete(request);
        return result;
    }

    // Create test JSON for listing catalogs
    static std::string createTestListCatalogsJsonConfig() {
        agentcli_cJSON *request = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(request, "IcebergConfig", createIcebergPolarisConfig());

        char *json_string = agentcli_cJSON_Print(request);
        std::string result(json_string);
        free(json_string);
        agentcli_cJSON_Delete(request);
        return result;
    }

    // Create test JSON for listing namespaces
    static std::string createTestListNamespacesJsonConfig(const std::string& catalog_name) {
        agentcli_cJSON *request = agentcli_cJSON_CreateObject();
        agentcli_cJSON_AddItemToObject(request, "IcebergConfig", createIcebergPolarisConfig());
        agentcli_cJSON_AddStringToObject(request, "catalogName", catalog_name.c_str());

        char *json_string = agentcli_cJSON_Print(request);
        std::string result(json_string);
        free(json_string);
        agentcli_cJSON_Delete(request);
        return result;
    }
};
