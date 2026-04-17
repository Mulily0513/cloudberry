#include <gtest/gtest.h>
#include "agent_client.hpp"
#include "json_builder.hpp"
#include "agent_c_api.h"
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

using namespace agent_cli;

class PerformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.server_url = "http://localhost:8080";
        config_.namespace_name = "perf_test";
        config_.request_timeout_seconds = 2;
        config_.connect_timeout_seconds = 1;
        config_.max_retries = 1;
        config_.retry_delay_ms = 50;
    }
    
    Config config_;
};

TEST_F(PerformanceTest, JsonBuilderPerformance) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // Build 1000 JSON objects
    for (int i = 0; i < 1000; ++i) {
        JsonBuilder builder;
        builder.add("key1", "value" + std::to_string(i))
               .add("key2", i)
               .add("key3", i % 2 == 0);
        std::string json = builder.build();
        EXPECT_GT(json.length(), 10);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Should complete within 1 second
    EXPECT_LT(duration.count(), 1000);
    std::cout << "JSON Builder: 1000 objects in " << duration.count() << "ms" << std::endl;
}

TEST_F(PerformanceTest, ConcurrentRequests) {
    AgentClient client(config_);
    
    const int num_threads = 10;
    const int requests_per_thread = 5;
    std::atomic<int> completed_requests{0};
    std::vector<std::thread> threads;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Launch concurrent threads
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&client, &completed_requests, requests_per_thread, t]() {
            for (int i = 0; i < requests_per_thread; ++i) {
                Response resp = client.load_table("ns_" + std::to_string(t), "table_" + std::to_string(i));
                completed_requests++;
            }
        });
    }
    
    // Wait for completion
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_EQ(completed_requests.load(), num_threads * requests_per_thread);
    
    // Should handle concurrent requests efficiently
    EXPECT_LT(duration.count(), 10000); // 10 seconds max
    
    std::cout << "Concurrent: " << completed_requests.load() << " requests in " 
              << duration.count() << "ms" << std::endl;
}

TEST_F(PerformanceTest, MemoryUsage) {
    // Test memory allocation/deallocation patterns
    std::vector<std::unique_ptr<AgentClient>> clients;
    
    // Create many clients
    for (int i = 0; i < 100; ++i) {
        clients.push_back(std::make_unique<AgentClient>(config_));
    }
    
    // Make requests with all clients
    for (auto& client : clients) {
        Response resp = client->table_exists("test_ns", "test_table");
        EXPECT_GE(resp.curl_code, 0);
    }
    
    // Cleanup (automatic via unique_ptr)
    clients.clear();
    
    SUCCEED(); // If we reach here without crash, memory management is working
}

TEST_F(PerformanceTest, CInterfaceOverhead) {
    agent_cli_config_t c_config = {};
    c_config.server_url = config_.server_url.c_str();
    c_config.prefix = config_.prefix.c_str();
    c_config.namespace_name = config_.namespace_name.c_str();
    c_config.request_timeout_seconds = config_.request_timeout_seconds;
    c_config.connect_timeout_seconds = config_.connect_timeout_seconds;
    c_config.max_retries = config_.max_retries;
    c_config.retry_delay_ms = config_.retry_delay_ms;
    
    agent_cli_handle_t handle = nullptr;
    ASSERT_EQ(agent_cli_init(&c_config, &handle), AGENT_CLI_SUCCESS);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Make 50 C interface calls
    for (int i = 0; i < 50; ++i) {
        agent_cli_response_t response = {};
        agent_cli_status_t status = agent_cli_load_table(handle, "perf_ns", "perf_table", &response);
        EXPECT_EQ(status, AGENT_CLI_SUCCESS);
        agent_cli_free_response(&response);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    agent_cli_cleanup(handle);
    
    // C interface should have minimal overhead
    EXPECT_LT(duration.count(), 5000); // 5 seconds max
    
    std::cout << "C Interface: 50 calls in " << duration.count() << "ms" << std::endl;
}

TEST_F(PerformanceTest, LargeJsonHandling) {
    // Test with large JSON payloads
    JsonBuilder builder;
    
    // Create large JSON with many fields
    for (int i = 0; i < 1000; ++i) {
        builder.add("field_" + std::to_string(i), "large_value_" + std::string(100, 'x') + std::to_string(i));
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    std::string large_json = builder.build();
    auto end = std::chrono::high_resolution_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    EXPECT_GT(large_json.length(), 100000); // Should be > 100KB
    EXPECT_LT(duration.count(), 10000); // Should complete in < 10ms
    
    std::cout << "Large JSON: " << large_json.length() << " bytes in " 
              << duration.count() << "μs" << std::endl;
}

TEST_F(PerformanceTest, StatisticsOverhead) {
    AgentClient client(config_);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Get statistics many times
    for (int i = 0; i < 10000; ++i) {
        long total = 0, failed = 0;
        client.get_stats(total, failed);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Statistics should be very fast
    EXPECT_LT(duration.count(), 100); // < 100ms for 10k calls
    
    std::cout << "Statistics: 10000 calls in " << duration.count() << "ms" << std::endl;
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    std::cout << "=== Performance Test Suite ===" << std::endl;
    int result = RUN_ALL_TESTS();
    std::cout << "=== Performance Tests Complete ===" << std::endl;
    
    return result;
}
