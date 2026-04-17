#include <gtest/gtest.h>
#include <agent_c_api.h>
#include "test_utils.hpp"
#include <cstring>


class CInterfaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_.server_url = "http://localhost:8080";
        // config_.prefix = "iceberg";
        config_.request_timeout_seconds = 3600;  // Short timeout
        config_.connect_timeout_seconds = 3600;
        config_.max_retries = 0;  // No retries
        config_.retry_delay_ms = 100;
        config_.auth_token = "";
        handle_ = nullptr;
        test_namespace = "default";
        test_table_name = TestUtils::generateUniqueTableName();
        request_json = TestUtils::createTestJsonConfig(test_table_name.c_str());

    }

    void TearDown() override {
        if (handle_) {
            agent_cli_cleanup(handle_);
        }
    }

    agent_cli_config_t config_;
    agent_cli_handle_t handle_;
    std::string test_namespace;
    std::string test_table_name;
    std::string request_json;
};

TEST_F(CInterfaceTest, Initialization) {
    agent_cli_status_t status = agent_cli_init(&config_, &handle_);
    EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    EXPECT_NE(handle_, nullptr);
}

TEST_F(CInterfaceTest, NullParameterValidation) {
    EXPECT_EQ(agent_cli_init(nullptr, &handle_), AGENT_CLI_ERROR_INVALID_PARAM);
    EXPECT_EQ(agent_cli_init(&config_, nullptr), AGENT_CLI_ERROR_INVALID_PARAM);
}

TEST_F(CInterfaceTest, ErrorStrings) {
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_SUCCESS), "Success");
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_ERROR_INVALID_PARAM), "Invalid parameter");
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_ERROR_NETWORK), "Network error");
}

// TEST_F(CInterfaceTest, Statistics) {
//     ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);

//     long total = 0, failed = 0;
//     agent_cli_status_t status = agent_cli_get_stats(handle_, &total, &failed);

//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(total, 0);
//     EXPECT_GE(failed, 0);
// }

// TEST_F(CInterfaceTest, ApiCallsStructure) {
//     ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);
//     agent_cli_response_t response = {};

//     // Test create table
//     agent_cli_status_t status = agent_cli_create_table(handle_, test_namespace.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_create_table:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);  // Should have valid curl code
//     agent_cli_free_response(&response);

//     // Test load table
//     memset(&response, 0, sizeof(response));
//     status = agent_cli_load_table(handle_, test_namespace.c_str(), test_table_name.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_load_table:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);

//     // Test fragment
//     memset(&response, 0, sizeof(response));
//     status = agent_cli_get_fragment(handle_, test_namespace.c_str(), test_table_name.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_get_fragment:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);

//     // Test table exists
//     memset(&response, 0, sizeof(response));
//     std::string unique_table2 = TestUtils::generateUniqueTableName();
//     status = agent_cli_table_exists(handle_, test_namespace.c_str(), test_table_name.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_table_exists:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);
// }

// TEST_F(CInterfaceTest, ResponseMemoryManagement) {
//     ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);

//     // Test multiple response allocations and frees
//     for (int i = 0; i < 3; ++i) {
//         agent_cli_response_t response = {};
//         agent_cli_status_t status = agent_cli_load_table(handle_, test_namespace.c_str(), test_table_name.c_str(), request_json.c_str(), &response);

//         // Expect network error, but response should be valid
//         EXPECT_EQ(status, AGENT_CLI_ERROR_NETWORK);

//         // Response should be properly initialized
//         EXPECT_GE(response.curl_code, 0);

//         // Free should not crash
//         agent_cli_free_response(&response);

//         // After free, pointers should be null
//         EXPECT_EQ(response.response_body, nullptr);
//         EXPECT_EQ(response.error_message, nullptr);
//     }
// }

// TEST_F(CInterfaceTest, CleanupSafety) {
//     // Cleanup with null should not crash
//     agent_cli_cleanup(nullptr);
//     SUCCEED();
// }

// TEST_F(CInterfaceTest, TestAppend) {
//     ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);
//     agent_cli_response_t response = {};

//     // Test create table
//     agent_cli_status_t status = agent_cli_create_table(handle_, test_namespace.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_create_table:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);  // Should have valid curl code
//     agent_cli_free_response(&response);

//     // Test append with proper AppendRequest format
//     memset(&response, 0, sizeof(response));

//     std::string append_json = TestUtils::createTestAppendJsonConfig(test_table_name.c_str());
//     status = agent_cli_append_table(handle_, test_namespace.c_str(), test_table_name.c_str(), append_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_append_table:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);

//     // Test fragment after append to verify data
//     memset(&response, 0, sizeof(response));
//     status = agent_cli_get_fragment(handle_, test_namespace.c_str(), test_table_name.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_get_fragment after append:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);
// }

// TEST_F(CInterfaceTest, TestUpdate) {
//     ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);
//     agent_cli_response_t response = {};

//     // Test create table
//     agent_cli_status_t status = agent_cli_create_table(handle_, test_namespace.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_create_table:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);  // Should have valid curl code
//     agent_cli_free_response(&response);

//     // Test append with proper AppendRequest format
//     memset(&response, 0, sizeof(response));

//     std::string append_json = TestUtils::createTestAppendJsonConfig(test_table_name.c_str());
//     status = agent_cli_append_table(handle_, test_namespace.c_str(), test_table_name.c_str(), append_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_append_table:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);

//     // Test fragment after append to verify data
//     memset(&response, 0, sizeof(response));
//     status = agent_cli_get_fragment(handle_, test_namespace.c_str(), test_table_name.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_get_fragment after append:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);

//     memset(&response, 0, sizeof(response));
//     append_json = TestUtils::createTestUpdateJsonConfig(test_table_name.c_str());
//     status = agent_cli_update_table(handle_, test_namespace.c_str(), test_table_name.c_str(), append_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_update_table:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);

//     memset(&response, 0, sizeof(response));
//     status = agent_cli_get_fragment(handle_, test_namespace.c_str(), test_table_name.c_str(), request_json.c_str(), &response);
//     TestUtils::print_formatted_response("agent_cli_get_fragment after append:", response.response_body);
//     EXPECT_EQ(status, AGENT_CLI_SUCCESS);
//     EXPECT_GE(response.curl_code, 0);
//     agent_cli_free_response(&response);
// }

TEST_F(CInterfaceTest, CatalogManagementApis) {
    ASSERT_EQ(agent_cli_init(&config_, &handle_), AGENT_CLI_SUCCESS);
    agent_cli_response_t response = {};

    // Test create catalog
    // std::string catalog_json = TestUtils::createTestCatalogJsonConfig("test_catalog", "default");
    // agent_cli_status_t status = agent_cli_create_catalog(handle_, catalog_json.c_str(), &response);
    // TestUtils::print_formatted_response("agent_cli_create_catalog:", response.response_body);
    // EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    // EXPECT_GE(response.curl_code, 0);
    // agent_cli_free_response(&response);

    // // Test list catalogs
    // memset(&response, 0, sizeof(response));
    // std::string list_catalogs_json = TestUtils::createTestListCatalogsJsonConfig();
    // status = agent_cli_list_catalogs(handle_, list_catalogs_json.c_str(), &response);
    // TestUtils::print_formatted_response("agent_cli_list_catalogs:", response.response_body);
    // EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    // EXPECT_GE(response.curl_code, 0);
    // agent_cli_free_response(&response);

    // // Test list namespaces
    // memset(&response, 0, sizeof(response));
    // std::string list_namespaces_json = TestUtils::createTestListNamespacesJsonConfig("test_catalog");
    // status = agent_cli_list_namespaces(handle_, list_namespaces_json.c_str(), &response);
    // TestUtils::print_formatted_response("agent_cli_list_namespaces:", response.response_body);
    // EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    // EXPECT_GE(response.curl_code, 0);
    // agent_cli_free_response(&response);

    // Test create namespace
    memset(&response, 0, sizeof(response));
    std::string namespace_json = TestUtils::createTestNamespaceJsonConfig("quickstart_catalog", "test_namespace");
    agent_cli_status_t status = agent_cli_create_namespace(handle_, "quickstart_catalog", "test_namespace", namespace_json.c_str(), &response);
    TestUtils::print_formatted_response("agent_cli_create_namespace:", response.response_body);
    EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    EXPECT_GE(response.curl_code, 0);
    agent_cli_free_response(&response);
    TestUtils::print_formatted_response("agent_cli_list_namespaces:", response.response_body);
    EXPECT_EQ(status, AGENT_CLI_SUCCESS);
    EXPECT_GE(response.curl_code, 0);
    agent_cli_free_response(&response);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
