cmake_minimum_required(VERSION 3.10)
project(kanren)

add_library(kanren SHARED kanren.cpp ../core/cursor.cpp ../core/debug.cpp ../listree/listree.cpp)
target_include_directories(kanren PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(kanren PRIVATE listree)

add_executable(kanren_tests tests/kanren_tests.cpp kanren.cpp ../core/cursor.cpp ../core/debug.cpp ../listree/listree.cpp)
target_link_libraries(kanren_tests PRIVATE listree gtest gtest_main pthread)
add_test(NAME kanren_tests COMMAND kanren_tests)
