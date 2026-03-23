cmake_minimum_required(VERSION 3.10)
project(kanren)

add_library(kanren SHARED kanren.cpp logic_evaluator.cpp runtime_ffi.cpp)
target_include_directories(kanren PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(kanren PUBLIC reflect)

add_executable(kanren_tests tests/kanren_tests.cpp)
target_include_directories(kanren_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(kanren_tests PRIVATE kanren reflect gtest gtest_main pthread)
add_test(NAME kanren_tests COMMAND kanren_tests)
