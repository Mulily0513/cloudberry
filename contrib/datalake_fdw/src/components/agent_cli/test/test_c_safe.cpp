#include <gtest/gtest.h>
#include <agent_c_api.h>

TEST(CInterfaceTest, ErrorStrings) {
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_SUCCESS), "Success");
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_ERROR_INVALID_PARAM), "Invalid parameter");
    EXPECT_STREQ(agent_cli_get_error_string(AGENT_CLI_ERROR_NETWORK), "Network error");
}

TEST(CInterfaceTest, NullParameterValidation) {
    agent_cli_config_t config = {};
    agent_cli_handle_t handle = nullptr;

    // These should return immediately without network calls
    EXPECT_EQ(agent_cli_init(nullptr, &handle), AGENT_CLI_ERROR_INVALID_PARAM);
    EXPECT_EQ(agent_cli_init(&config, nullptr), AGENT_CLI_ERROR_INVALID_PARAM);

    // Test null server_url
    config.server_url = nullptr;
    EXPECT_EQ(agent_cli_init(&config, &handle), AGENT_CLI_ERROR_INVALID_PARAM);

    // Test empty server_url
    config.server_url = "";
    EXPECT_EQ(agent_cli_init(&config, &handle), AGENT_CLI_ERROR_INVALID_PARAM);
}

TEST(CInterfaceTest, CleanupSafety) {
    // Cleanup with null should not crash
    agent_cli_cleanup(nullptr);
    SUCCEED();
}

TEST(CInterfaceTest, ResponseFreeSafety) {
    agent_cli_response_t response = {};

    // Free empty response should not crash
    agent_cli_free_response(&response);
    agent_cli_free_response(nullptr);
    SUCCEED();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
