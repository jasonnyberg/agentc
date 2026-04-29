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

add_executable(cpp-agent
    main.cpp agent_loop.cpp api_registry.cpp sse_parser.cpp http_client.cpp
    socket_server.cpp credentials.cpp edict_tools.cpp providers/google.cpp
    providers/openai.cpp
)

target_include_directories(cpp-agent PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${nlohmann_json_SOURCE_DIR}
    ${asio_SOURCE_DIR}/asio/include
)

target_link_libraries(cpp-agent PRIVATE libedict CURL::libcurl)
target_compile_options(cpp-agent PRIVATE -Wall -Wextra -Wno-unused-parameter -DASIO_STANDALONE)
