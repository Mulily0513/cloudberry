#include <gtest/gtest.h>
#include "agent_client.hpp"
#include "json_builder.hpp"
#include "logger.hpp"
#include "agent_c_api.h"
#include <thread>
#include <chrono>

using namespace agent_cli;

class AgentCliTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        config_.server_url = "http://localhost:8080/api/v1";
        config_.auth_token = "";
        config_.request_timeout_seconds = 5;
        config_.connect_timeout_seconds = 3;
        config_.max_retries = 2;
        config_.retry_delay_ms = 100;
    }

    Config config_;
};

TEST(LoggerTest, Initialization)
{
    Logger::instance().init("./test_agent_cli.log", LogLevel::DEBUG);

    LOG_INFO("Test info message");
    LOG_DEBUG("Test debug message");
    LOG_ERROR("Test error message");

    // No assertion needed, just verify no crash
    SUCCEED();
}

TEST(LoggerTest, LogLevels)
{
    Logger::instance().init("", LogLevel::ERROR);

    // These should not crash
    LOG_ERROR("Error message");
    LOG_WARN("Warn message");
    LOG_INFO("Info message");
    LOG_DEBUG("Debug message");

    SUCCEED();
}

// Agent Client Tests
TEST_F(AgentCliTest, ClientCreation)
{
    EXPECT_NO_THROW({
        AgentClient client(config_);
    });
}

TEST_F(AgentCliTest, UrlBuilding)
{
    AgentClient client(config_);

    // Test through public interface by checking error messages
    Response response = client.create_table("test_ns", "{}");
    EXPECT_GE(response.curl_code, 0); // Should have valid curl code
}

TEST_F(AgentCliTest, Statistics)
{
    AgentClient client(config_);

    long total = 0, failed = 0;
    client.get_stats(total, failed);

    EXPECT_GE(total, 0);
    EXPECT_GE(failed, 0);
}

TEST_F(AgentCliTest, AllApiEndpoints)
{
    AgentClient client(config_);

    // Test all API endpoints (will fail due to no server, but should not crash)
    Response resp1 = client.create_table("ns", "{}");
    Response resp2 = client.load_table("ns", "table", "{}", nullptr);
    Response resp3 = client.table_exists("ns", "table", "{}", nullptr);
    Response resp4 = client.get_fragment("ns", "table", "{}");
    Response resp5 = client.append_table("ns", "table", "{}");

    // All should have valid structure
    EXPECT_GE(resp1.curl_code, 0);
    EXPECT_GE(resp2.curl_code, 0);
    EXPECT_GE(resp3.curl_code, 0);
    EXPECT_GE(resp4.curl_code, 0);
    EXPECT_GE(resp5.curl_code, 0);
}

// C Interface Tests
class CInterfaceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        config_.server_url = "http://localhost:8080/api/v1";
        config_.namespace_name = "default";
        config_.auth_token = "";
        config_.request_timeout_seconds = 1;
        config_.connect_timeout_seconds = 1;
        config_.max_retries = 0;
        config_.retry_delay_ms = 100;

        handle_ = nullptr;
    }

    void TearDown() override
    {
        if (handle_)
        {
            agent_cli_cleanup(handle_);
        }
    }

    agent_cli_config_t config_;
    agent_cli_handle_t handle_;
};

TEST_F(CInterfaceTest, Initialization)
{
    agent_cli_status_t status = agent_cli_init(&config_, &handle_);
    EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    EXPECT_NE(handle_, nullptr);
}

TEST_F(CInterfaceTest, NullParameterValidation)
{
    EXPECT_EQ(agent_cli_init(nullptr, &handle_), AGENT_CLI_ERROR_INVALID_PARAM);
    EXPECT_EQ(agent_cli_init(&config_, nullptr), AGENT_CLI_ERROR_INVALID_PARAM);
}

TEST_F(CInterfaceTest, ErrorStrings)
{
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_SUCCESS), "Success");
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_ERROR_INVALID_PARAM), "Invalid parameter");
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_ERROR_NETWORK), "Network error");
}

TEST_F(CInterfaceTest, Statistics)
{
    ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);

    long total = 0, failed = 0;
    agent_cli_status_t status = agent_cli_get_stats(handle_, &total, &failed);

    EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    EXPECT_GE(total, 0);
    EXPECT_GE(failed, 0);
}

TEST_F(CInterfaceTest, ApiCalls)
{
    Logger::instance().init("./test_agent_cli.log", LogLevel::DEBUG);
    ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);
    agent_cli_response_t response = {};
    agent_cli_status_t status = agent_cli_create_table(handle_, "test_ns", "{}", &response);
    EXPECT_EQ(status, AGENT_CLI_ERROR_NETWORK);
    EXPECT_GE(response.curl_code, 0);
    agent_cli_free_response(&response);

    memset(&response, 0, sizeof(response));
    status = agent_cli_load_table(handle_, "test_ns", "test_table", "{}", &response);
    EXPECT_EQ(status, AGENT_CLI_ERROR_NETWORK);
    EXPECT_GE(response.curl_code, 0);
    agent_cli_free_response(&response);
    memset(&response, 0, sizeof(response));
    status = agent_cli_table_exists(handle_, "test_ns", "test_table", "{}", &response);
    EXPECT_EQ(status, AGENT_CLI_ERROR_NETWORK);
    EXPECT_GE(response.curl_code, 0);
    agent_cli_free_response(&response);
}

TEST_F(CInterfaceTest, ResponseMemoryManagement)
{
    ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);
    for (int i = 0; i < 3; ++i)
    {
        agent_cli_response_t response = {};
        agent_cli_status_t status = agent_cli_load_table(handle_, "test_ns", "test_table", "{}", &response);
        EXPECT_EQ(status, AGENT_CLI_ERROR_NETWORK);
        EXPECT_GE(response.curl_code, 0);
        EXPECT_NO_THROW(agent_cli_free_response(&response));
        EXPECT_EQ(response.response_body, nullptr);
        EXPECT_EQ(response.error_message, nullptr);
    }
}

// Thread Safety Tests
TEST_F(AgentCliTest, ThreadSafety)
{
    AgentClient client(config_);

    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};

    // Launch multiple threads making requests
    for (int i = 0; i < 5; ++i)
    {
        threads.emplace_back([&client, &success_count, i]()
                             {
            Response resp = client.load_table("ns" + std::to_string(i), "table" + std::to_string(i), "{}", nullptr);
            if (resp.curl_code >= 0) {
                success_count++;
            } });
    }

    // Wait for all threads
    for (auto &t : threads)
    {
        t.join();
    }

    EXPECT_EQ(success_count.load(), 5);

    // Check final statistics
    long total = 0, failed = 0;
    client.get_stats(total, failed);
    EXPECT_GE(total, 5);
}

// Performance Tests
TEST_F(AgentCliTest, PerformanceBaseline)
{
    AgentClient client(config_);

    auto start = std::chrono::high_resolution_clock::now();

    // Make multiple requests
    for (int i = 0; i < 10; ++i)
    {
        client.load_table("perf_ns", "perf_table", "{}", nullptr);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    // Should complete within reasonable time (even with failures)
    EXPECT_LT(duration.count(), 30000); // 30 seconds max

    long total = 0, failed = 0;
    client.get_stats(total, failed);
    EXPECT_GE(total, 10);
}

// Configuration Tests
TEST(ConfigTest, DefaultValues)
{
    Config config;
    config.server_url = "http://test.com";

    EXPECT_NO_THROW({
        AgentClient client(config);
    });
}

TEST(ConfigTest, InvalidConfiguration)
{
    Config config;
    // Empty server_url should still work (will fail at request time)

    EXPECT_NO_THROW({
        AgentClient client(config);
    });
}

// Integration Test Suite
class IntegrationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Setup for integration tests
        Logger::instance().init("/tmp/integration_test.log", LogLevel::DEBUG);
    }
};

TEST_F(IntegrationTest, FullWorkflow)
{
    // Test complete workflow: init -> create -> load -> exists -> append -> cleanup
    agent_cli_config_t config = {};
    config.server_url = "http://localhost:8080";
    config.namespace_name = "integration_test";
    config.request_timeout_seconds = 10;
    config.connect_timeout_seconds = 5;
    config.max_retries = 1;
    config.retry_delay_ms = 500;

    agent_cli_handle_t handle = nullptr;
    ASSERT_EQ(agent_cli_init(&config, &handle), AGENT_CLI_SUCCESS);

    agent_cli_response_t response = {};

    // Create table
    std::string create_json = JsonBuilder::create_table_request("{}", "integration_table", "{}");
    EXPECT_EQ(agent_cli_create_table(handle, "integration_ns", create_json.c_str(), &response), AGENT_CLI_SUCCESS);
    agent_cli_free_response(&response);

    // Load table
    EXPECT_EQ(agent_cli_load_table(handle, "integration_ns", "integration_table", "{}", &response), AGENT_CLI_SUCCESS);
    agent_cli_free_response(&response);

    // Check exists
    EXPECT_EQ(agent_cli_table_exists(handle, "integration_ns", "integration_table", "{}", &response), AGENT_CLI_SUCCESS);
    agent_cli_free_response(&response);

    // Get fragment
    std::string fragment_json = "{}";
    EXPECT_EQ(agent_cli_get_fragment(handle, "integration_ns", "integration_table", fragment_json.c_str(), &response), AGENT_CLI_SUCCESS);
    agent_cli_free_response(&response);

    // Append data
    std::string append_json = "{}";
    EXPECT_EQ(agent_cli_append_table(handle, "integration_ns", "integration_table", append_json.c_str(), &response), AGENT_CLI_SUCCESS);
    agent_cli_free_response(&response);

    // Check final statistics
    long total = 0, failed = 0;
    EXPECT_EQ(agent_cli_get_stats(handle, &total, &failed), AGENT_CLI_SUCCESS);
    EXPECT_GE(total, 5); // At least 5 requests made

    agent_cli_cleanup(handle);
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
