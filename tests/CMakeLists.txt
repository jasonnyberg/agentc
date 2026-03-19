cmake_minimum_required(VERSION 3.10)
project(AgentCTests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR})

# Core library tests (allocator, container, cursor, string architecture)
add_executable(reflect_tests alloc_tests.cpp container_tests.cpp test_main.cpp string_architecture_tests.cpp)
target_link_libraries(reflect_tests reflect gtest_main pthread)
add_test(NAME reflect_tests COMMAND reflect_tests)
