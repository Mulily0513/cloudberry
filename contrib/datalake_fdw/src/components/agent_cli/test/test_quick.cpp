#include <gtest/gtest.h>
#include <agent_client.hpp>
#include <json_builder.hpp>
#include <logger.hpp>
#include <agent_c_api.h>

using namespace agent_cli;

TEST(JsonBuilderTest, BasicOperations) {
    JsonBuilder builder;
    std::string key1 = "string_key";
    std::string val1 = "test_value";
    builder.add(key1, val1);
    builder.add("int_key", 42);
    builder.add("bool_key", true);
    
    std::string json = builder.build();
    EXPECT_NE(json.find("test_value"), std::string::npos);
    EXPECT_NE(json.find("42"), std::string::npos);
    EXPECT_NE(json.find("true"), std::string::npos);
}

TEST(LoggerTest, Initialization) {
    // Use just filename, logger will auto-detect directory
    Logger::instance().init("test_agent_cli.log", LogLevel::DEBUG);
    
    LOG_INFO("Test info message");
    LOG_DEBUG("Test debug message");
    LOG_ERROR("Test error message");
    
    SUCCEED();
}

TEST(CInterfaceTest, ErrorStrings) {
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_SUCCESS), "Success");
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_ERROR_INVALID_PARAM), "Invalid parameter");
}

TEST(CInterfaceTest, NullValidation) {
    agent_cli_config_t config = {};
    agent_cli_handle_t handle = nullptr;
    
    EXPECT_EQ(agent_cli_init(nullptr, &handle), AGENT_CLI_ERROR_INVALID_PARAM);
    EXPECT_EQ(agent_cli_init(&config, nullptr), AGENT_CLI_ERROR_INVALID_PARAM);
}

TEST(CInterfaceTest, QuickInit) {
    agent_cli_config_t config = {};
    config.server_url = "http://localhost:8080";
    config.request_timeout_seconds = 1;
    config.connect_timeout_seconds = 1;
    config.max_retries = 0;
    
    agent_cli_handle_t handle = nullptr;
    agent_cli_status_t status = agent_cli_init(&config, &handle);
    
    EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    if (handle) {
        agent_cli_cleanup(handle);
    }
}

TEST(AgentClientTest, QuickCreation) {
    Config config;
    config.server_url = "http://localhost:8080";
    config.request_timeout_seconds = 1;
    config.connect_timeout_seconds = 1;
    config.max_retries = 0;
    
    AgentClient client(config);
    long total = 0, failed = 0;
    client.get_stats(total, failed);
    EXPECT_GE(total, 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
