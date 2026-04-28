cmake_minimum_required(VERSION 3.16)
project(cpp-agent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Dependencies ────────────────────────────────────────────────────────────

# nlohmann/json (header-only, fetched if not installed)
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_GetProperties(nlohmann_json)
if(NOT nlohmann_json_POPULATED)
    FetchContent_Populate(nlohmann_json)
    # Single header downloaded to nlohmann_json_SOURCE_DIR/json.hpp
    # Create a small wrapper include dir
    file(COPY ${nlohmann_json_SOURCE_DIR}/json.hpp
         DESTINATION ${nlohmann_json_SOURCE_DIR}/nlohmann/)
endif()

# libcurl
find_package(CURL REQUIRED)

# ─── Tests ────────────────────────────────────────────────────────────────────

add_executable(cpp-agent-tests
    tests/agent_loop_test.cpp
    tests/provider_registration_test.cpp
    tests/multi_provider_test.cpp
    tests/provider_integration_demo.cpp
    agent_loop.cpp
    api_registry.cpp
    sse_parser.cpp
    http_client.cpp
    credentials.cpp
    edict_tools.cpp
    providers/google.cpp
    providers/openai.cpp
)
target_link_libraries(cpp-agent-tests PRIVATE libedict CURL::libcurl gtest gtest_main)
target_include_directories(cpp-agent-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} ${nlohmann_json_SOURCE_DIR})
target_compile_options(cpp-agent-tests PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ─── Executable ──────────────────────────────────────────────────────────────

add_executable(cpp-agent
    main.cpp
    agent_loop.cpp
    api_registry.cpp
    sse_parser.cpp
    http_client.cpp
    credentials.cpp
    edict_tools.cpp
    providers/google.cpp
    providers/openai.cpp
)

# ─── Include paths ───────────────────────────────────────────────────────────

target_include_directories(cpp-agent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # cpp-agent/ itself
    ${CMAKE_SOURCE_DIR}                  # project root (for edict/edict_vm.h etc.)
    ${nlohmann_json_SOURCE_DIR}          # nlohmann/json.hpp
)

# ─── Linked libraries ────────────────────────────────────────────────────────

target_link_libraries(cpp-agent PRIVATE
    libedict          # Edict VM + compiler + REPL
    CURL::libcurl     # HTTP + SSE
)

# ─── Compile options ─────────────────────────────────────────────────────────

target_compile_options(cpp-agent PRIVATE -Wall -Wextra -Wno-unused-parameter)
