#include <gtest/gtest.h>
#include <agent_client.hpp>
#include <json_builder.hpp>
#include <logger.hpp>
#include <agent_c_api.h>

using namespace agent_cli;

class StableTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.server_url = "http://localhost:8080";
        config_.namespace_name = "test";
        config_.request_timeout_seconds = 1;
        config_.connect_timeout_seconds = 1;
        config_.max_retries = 0;
        config_.retry_delay_ms = 100;
    }
    
    Config config_;
};

TEST(JsonBuilderTest, BasicConstruction) {
    JsonBuilder builder;
    std::string json = builder.build();
    EXPECT_EQ(json, "{}");
}

TEST(JsonBuilderTest, SingleField) {
    JsonBuilder builder;
    builder.add("test", "value");
    std::string json = builder.build();
    EXPECT_NE(json.find("test"), std::string::npos);
}

TEST(JsonBuilderTest, StaticHelpers) {
    std::string create_req = JsonBuilder::create_table_request("{}", "test_table", "{}");
    EXPECT_GT(create_req.length(), 10);
}

TEST(LoggerTest, BasicLogging) {
    Logger::instance().init("/tmp/stable_test.log", LogLevel::DEBUG);
    EXPECT_NO_THROW(LOG_INFO("Test message"));
}

TEST_F(StableTest, ClientCreation) {
    EXPECT_NO_THROW({
        AgentClient client(config_);
    });
}

TEST(CInterfaceTest, ErrorStrings) {
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_SUCCESS), "Success");
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_ERROR_INVALID_PARAM), "Invalid parameter");
}

TEST(CInterfaceTest, ValidInitialization) {
    agent_cli_config_t config = {};
    config.server_url = "http://localhost:8080";
    config.prefix = "iceberg";
    config.request_timeout_seconds = 1;
    
    agent_cli_handle_t handle = nullptr;
    agent_cli_status_t status = agent_cli_init(&config, &handle);
    
    EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    EXPECT_NE(handle, nullptr);
    
    if (handle) {
        agent_cli_cleanup(handle);
    }
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
