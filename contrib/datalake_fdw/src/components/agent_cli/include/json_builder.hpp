#ifndef JSON_BUILDER_HPP
#define JSON_BUILDER_HPP

#include <string>

namespace agent_cli {

class JsonBuilder {
public:
    JsonBuilder();
    ~JsonBuilder();
    
    JsonBuilder& add(const std::string& key, const std::string& value);
    JsonBuilder& add(const std::string& key, int value);
    JsonBuilder& add(const std::string& key, bool value);
    
    std::string build() const;
    
    static std::string create_table_request(const std::string& iceberg_config,
                                          const std::string& table_name,
                                          const std::string& schema_json);

private:
    void* json_obj_;
};

} // namespace agent_cli

#endif // JSON_BUILDER_HPP
