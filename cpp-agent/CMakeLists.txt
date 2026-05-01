cmake_minimum_required(VERSION 3.16)
project(cpp-agent)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)

FetchContent_Declare(asio URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz)
FetchContent_MakeAvailable(asio)

FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(nlohmann_json)
file(COPY ${nlohmann_json_SOURCE_DIR}/json.hpp DESTINATION ${nlohmann_json_SOURCE_DIR}/nlohmann/)

find_package(CURL REQUIRED)

add_library(agent_runtime SHARED
    runtime/core/runtime.cpp
    runtime/core/provider_registry.cpp
    runtime/c_api/agentc_runtime_c_api.cpp
    runtime/providers/register_builtin_providers.cpp
    runtime/common/credentials.cpp
    runtime/common/http_client.cpp
    runtime/common/sse_parser.cpp
    runtime/persistence/session_state_store.cpp
    runtime/providers/google/google_provider.cpp
    runtime/providers/openai/openai_provider.cpp
)
set_target_properties(agent_runtime PROPERTIES OUTPUT_NAME agent_runtime)

target_include_directories(agent_runtime
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${nlohmann_json_SOURCE_DIR}
)

target_link_libraries(agent_runtime PRIVATE CURL::libcurl)
target_compile_options(agent_runtime PRIVATE -Wall -Wextra -Wno-unused-parameter)

add_executable(cpp-agent
    main.cpp
    socket_server.cpp
)

target_include_directories(cpp-agent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${nlohmann_json_SOURCE_DIR}
    ${asio_SOURCE_DIR}/asio/include
)

target_link_libraries(cpp-agent PRIVATE libedict agent_runtime CURL::libcurl)
target_compile_options(cpp-agent PRIVATE -Wall -Wextra -Wno-unused-parameter -DASIO_STANDALONE)

add_executable(hello_gemini hello_gemini.cpp)
target_link_libraries(hello_gemini PRIVATE CURL::libcurl)
target_include_directories(hello_gemini PRIVATE ${nlohmann_json_SOURCE_DIR})
target_compile_options(hello_gemini PRIVATE -Wall -Wextra)

add_executable(cpp_agent_tests
    tests/runtime_abi_test.cpp
    tests/edict_agentc_module_test.cpp
    tests/edict_hello_loop_test.cpp
    tests/edict_stateful_loop_test.cpp
    tests/session_state_store_test.cpp
)
target_compile_definitions(cpp_agent_tests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
)
target_include_directories(cpp_agent_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${nlohmann_json_SOURCE_DIR}
)
target_link_libraries(cpp_agent_tests PRIVATE agent_runtime libedict gtest gtest_main pthread)
add_dependencies(cpp_agent_tests agent_runtime agentc_extensions)
add_test(NAME cpp_agent_tests COMMAND cpp_agent_tests)
