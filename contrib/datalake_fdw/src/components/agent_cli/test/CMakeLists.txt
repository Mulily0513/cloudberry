# Test executables
# add_executable(test_quick test_quick.cpp)
# target_link_libraries(test_quick agent_cli_static CURL::libcurl dl GTest::GTest GTest::Main)

# add_executable(test_regression test_regression.cpp)
# target_link_libraries(test_regression agent_cli_static CURL::libcurl dl GTest::GTest GTest::Main)

add_executable(test_c_interface test_c_interface.cpp)
target_link_libraries(test_c_interface agent_cli_static CURL::libcurl GTest::GTest GTest::Main dl pthread)

add_executable(test_gtest test_gtest.cpp)
target_link_libraries(test_gtest agent_cli_static CURL::libcurl GTest::GTest GTest::Main dl pthread)

# add_executable(test_performance test_performance.cpp)
# target_link_libraries(test_performance agent_cli_static CURL::libcurl dl GTest::GTest GTest::Main)

# add_executable(test_stable test_stable.cpp)
# target_link_libraries(test_stable agent_cli_static CURL::libcurl dl GTest::GTest GTest::Main)

# add_executable(test_json_fixed test_json_fixed.cpp)
# target_link_libraries(test_json_fixed agent_cli_static CURL::libcurl dl GTest::GTest GTest::Main)

# add_executable(test_c_safe test_c_safe.cpp)
# target_link_libraries(test_c_safe agent_cli_static CURL::libcurl dl GTest::GTest GTest::Main)

# Register tests
# add_test(NAME QuickTests COMMAND test_quick)
# add_test(NAME RegressionTests COMMAND test_regression)
add_test(NAME CInterfaceTests COMMAND test_c_interface)
add_test(NAME GTests COMMAND test_gtest)
# add_test(NAME PerformanceTests COMMAND test_performance)
# add_test(NAME StableTests COMMAND test_stable)
# add_test(NAME JsonFixedTests COMMAND test_json_fixed)
# add_test(NAME CSafeTests COMMAND test_c_safe)
