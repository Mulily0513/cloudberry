#include <gtest/gtest.h>
#include "agent_client.hpp"
#include "json_builder.hpp"
#include "logger.hpp"
#include "agent_c_api.h"
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

using namespace agent_cli;

// Regression tests for specific bugs and edge cases
class RegressionTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.server_url = "http://localhost:8080";
        config_.namespace_name = "regression_test";
        config_.request_timeout_seconds = 5;
        config_.connect_timeout_seconds = 3;
        config_.max_retries = 1;
        config_.retry_delay_ms = 100;
    }
    
    Config config_;
};

// Test for empty/null string handling
TEST_F(RegressionTest, EmptyStringHandling) {
    JsonBuilder builder;
    
    // Empty strings should be handled correctly
    builder.add("empty_key", "");
    std::string json = builder.build();
    
    EXPECT_NE(json.find("empty_key"), std::string::npos);
    EXPECT_NE(json.find("\"\""), std::string::npos); // Should contain empty quotes
}

// Test for special characters in JSON
TEST_F(RegressionTest, SpecialCharactersInJson) {
    JsonBuilder builder;
    
    // Test various special characters
    builder.add("quotes", "value\"with\"quotes")
           .add("newlines", "value\nwith\nnewlines")
           .add("tabs", "value\twith\ttabs")
           .add("backslash", "value\\with\\backslash");
    
    std::string json = builder.build();
    
    // Should contain escaped characters
    EXPECT_NE(json.find("\\\""), std::string::npos);
    EXPECT_NE(json.find("\\n"), std::string::npos);
    EXPECT_NE(json.find("\\t"), std::string::npos);
    EXPECT_NE(json.find("\\\\"), std::string::npos);
}

// Test for URL building edge cases
TEST_F(RegressionTest, UrlBuildingEdgeCases) {
    AgentClient client(config_);
    
    // Test with various namespace and table name formats
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {"simple", "table"},
        {"namespace_with_underscore", "table_with_underscore"},
        {"ns-with-dash", "table-with-dash"},
        {"ns123", "table456"},
        {"", ""}, // Empty strings
    };
    
    for (const auto& test_case : test_cases) {
        Response resp = client.load_table(test_case.first, test_case.second);
        // Should not crash, curl_code should be valid
        EXPECT_GE(resp.curl_code, 0);
    }
}

// Test for configuration edge cases
TEST_F(RegressionTest, ConfigurationEdgeCases) {
    // Test with minimal configuration
    Config minimal_config;
    minimal_config.server_url = "http://test.com";
    
    EXPECT_NO_THROW({
        AgentClient client(minimal_config);
    });
    
    // Test with extreme timeout values
    Config extreme_config;
    extreme_config.server_url = "http://test.com";
    extreme_config.request_timeout_seconds = 1;
    extreme_config.connect_timeout_seconds = 1;
    extreme_config.max_retries = 0;
    extreme_config.retry_delay_ms = 1;
    
    EXPECT_NO_THROW({
        AgentClient client(extreme_config);
    });
}

// Test for C interface parameter validation
TEST(RegressionTest, CInterfaceParameterValidation) {
    agent_cli_config_t config = {};
    config.server_url = "http://localhost:8080";
    
    agent_cli_handle_t handle = nullptr;
    
    // Test null config
    EXPECT_EQ(agent_cli_init(nullptr, &handle), AGENT_CLI_ERROR_INVALID_PARAM);
    
    // Test null handle pointer
    EXPECT_EQ(agent_cli_init(&config, nullptr), AGENT_CLI_ERROR_INVALID_PARAM);
    
    // Test null server_url
    config.server_url = nullptr;
    EXPECT_EQ(agent_cli_init(&config, &handle), AGENT_CLI_ERROR_INVALID_PARAM);
    
    // Test empty server_url
    config.server_url = "";
    EXPECT_EQ(agent_cli_init(&config, &handle), AGENT_CLI_ERROR_INVALID_PARAM);
}

// Test for response memory management
TEST_F(RegressionTest, ResponseMemoryManagement) {
    agent_cli_config_t c_config = {};
    c_config.server_url = config_.server_url.c_str();
    c_config.prefix = config_.prefix.c_str();
    
    agent_cli_handle_t handle = nullptr;
    ASSERT_EQ(agent_cli_init(&c_config, &handle), AGENT_CLI_SUCCESS);
    
    // Test multiple response allocations and frees
    for (int i = 0; i < 10; ++i) {
        agent_cli_response_t response = {};
        agent_cli_status_t status = agent_cli_load_table(handle, "test_ns", "test_table", &response);
        
        EXPECT_EQ(status, AGENT_CLI_SUCCESS);
        
        // Free should be safe to call multiple times
        agent_cli_free_response(&response);
        agent_cli_free_response(&response); // Should not crash
    }
    
    agent_cli_cleanup(handle);
}

// Test for JSON builder with large data
TEST_F(RegressionTest, JsonBuilderLargeData) {
    JsonBuilder builder;
    
    // Add many fields
    for (int i = 0; i < 100; ++i) { // Reduced from 1000 to 100 for faster testing
        builder.add("key_" + std::to_string(i), "value_" + std::to_string(i));
    }
    
    std::string json = builder.build();
    
    // Should contain all keys
    EXPECT_NE(json.find("key_0"), std::string::npos);
    EXPECT_NE(json.find("key_99"), std::string::npos);
    EXPECT_GT(json.length(), 1000); // Should be reasonably large
}

// Test for error code consistency
TEST(RegressionTest, ErrorCodeConsistency) {
    // Test all error codes have valid strings
    for (int code = AGENT_CLI_SUCCESS; code >= AGENT_CLI_ERROR_CONFLICT; --code) {
        const char* error_str = agent_cli_get_error_string(static_cast<agent_cli_status_t>(code));
        EXPECT_NE(error_str, nullptr);
        EXPECT_GT(strlen(error_str), 0);
    }
    
    // Test invalid error code
    const char* invalid_str = agent_cli_get_error_string(static_cast<agent_cli_status_t>(-999));
    EXPECT_STREQ(invalid_str, "Unknown error");
}

// Test for cleanup edge cases
TEST_F(RegressionTest, CleanupEdgeCases) {
    // Test cleanup with null handle
    EXPECT_NO_THROW(agent_cli_cleanup(nullptr));
    
    // Test double cleanup
    agent_cli_config_t config = {};
    config.server_url = "http://localhost:8080";
    
    agent_cli_handle_t handle = nullptr;
    ASSERT_EQ(agent_cli_init(&config, &handle), AGENT_CLI_SUCCESS);
    
    agent_cli_cleanup(handle);
    EXPECT_NO_THROW(agent_cli_cleanup(handle)); // Should not crash
}

// Simplified thread safety test
TEST_F(RegressionTest, BasicThreadSafety) {
    AgentClient client(config_);
    
    std::atomic<int> completed{0};
    std::vector<std::thread> threads;
    
    // Launch a few threads
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&client, &completed, i]() {
            Response resp = client.load_table("thread_ns_" + std::to_string(i), "table");
            if (resp.curl_code >= 0) {
                completed++;
            }
        });
    }
    
    // Wait for completion
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(completed.load(), 3);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== Regression Test Suite ===" << std::endl;
    int result = RUN_ALL_TESTS();
    std::cout << "=== Regression Tests Complete ===" << std::endl;
    
    return result;
}
