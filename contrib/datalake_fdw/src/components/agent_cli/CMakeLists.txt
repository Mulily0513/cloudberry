cmake_minimum_required(VERSION 3.10)
project(agent_cli VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DAGENT_CLI_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

find_package(CURL REQUIRED)
find_package(GTest QUIET)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/c_interface)

set(AGENT_CLI_SOURCES
    src/agent_client.cpp
    src/http_client.cpp
    src/agent_types.cpp
    src/json_builder.cpp
    src/agent_cjson_builder.cpp
    src/logger.cpp
    src/agent_c_api.cpp
)

add_library(agent_cli SHARED ${AGENT_CLI_SOURCES})
target_link_libraries(agent_cli CURL::libcurl dl)
target_compile_options(agent_cli PRIVATE -Wall -Wextra -fPIC)

# Set library properties for versioning
set_target_properties(agent_cli PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME agent_cli
)

add_library(agent_cli_static STATIC ${AGENT_CLI_SOURCES})
target_link_libraries(agent_cli_static CURL::libcurl dl)
target_compile_options(agent_cli_static PRIVATE -Wall -Wextra -fPIC)
set_target_properties(agent_cli_static PROPERTIES OUTPUT_NAME agent_cli)

option(ENABLE_TEST "Enable building tests" OFF)

if(ENABLE_TEST AND GTest_FOUND)
    enable_testing()
    add_subdirectory(test)
endif()

install(TARGETS agent_cli agent_cli_static
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)

install(DIRECTORY include/ DESTINATION include
    FILES_MATCHING PATTERN "*.hpp")

install(FILES c_interface/agent_c_api.h c_interface/agent_cjson_builder.hpp
    DESTINATION include)

# Create symlinks after build
add_custom_command(TARGET agent_cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink 
        libagent_cli.so.${PROJECT_VERSION} 
        ${CMAKE_CURRENT_BINARY_DIR}/libagent_cli.so.${PROJECT_VERSION_MAJOR}
    COMMAND ${CMAKE_COMMAND} -E create_symlink 
        libagent_cli.so.${PROJECT_VERSION_MAJOR} 
        ${CMAKE_CURRENT_BINARY_DIR}/libagent_cli.so
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
