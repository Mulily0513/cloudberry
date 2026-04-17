#include "json_builder.hpp"
#include "agent_cjson_builder.hpp"

namespace agent_cli {

JsonBuilder::JsonBuilder() {
    json_obj_ = agentcli_cJSON_CreateObject();
}

JsonBuilder::~JsonBuilder() {
    if (json_obj_) {
        agentcli_cJSON_Delete(static_cast<agentcli_cJSON*>(json_obj_));
    }
}

JsonBuilder& JsonBuilder::add(const std::string& key, const std::string& value) {
    agentcli_cJSON_AddStringToObject(static_cast<agentcli_cJSON*>(json_obj_), key.c_str(), value.c_str());
    return *this;
}

JsonBuilder& JsonBuilder::add(const std::string& key, int value) {
    agentcli_cJSON_AddNumberToObject(static_cast<agentcli_cJSON*>(json_obj_), key.c_str(), value);
    return *this;
}

JsonBuilder& JsonBuilder::add(const std::string& key, bool value) {
    agentcli_cJSON_AddBoolToObject(static_cast<agentcli_cJSON*>(json_obj_), key.c_str(), value);
    return *this;
}

std::string JsonBuilder::build() const {
    char* json_str = agentcli_cJSON_PrintUnformatted(static_cast<agentcli_cJSON*>(json_obj_));
    std::string result(json_str);
    free(json_str);
    return result;
}

std::string JsonBuilder::create_table_request(const std::string& iceberg_config,
                                            const std::string& table_name,
                                            const std::string& schema_json) {
    agentcli_cJSON *request = agentcli_cJSON_CreateObject();
    agentcli_cJSON_AddStringToObject(request, "IcebergConfig", iceberg_config.c_str());
    agentcli_cJSON_AddStringToObject(request, "name", table_name.c_str());
    if (!schema_json.empty()) {
        agentcli_cJSON_AddStringToObject(request, "schema", schema_json.c_str());
    }
    
    char* json_str = agentcli_cJSON_PrintUnformatted(request);
    std::string result(json_str);
    free(json_str);
    agentcli_cJSON_Delete(request);
    return result;
}

} // namespace agent_cli
