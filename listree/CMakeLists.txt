cmake_minimum_required(VERSION 3.10)

add_library(listree STATIC listree.cpp)
set_property(TARGET listree PROPERTY POSITION_INDEPENDENT_CODE ON)
target_include_directories(listree PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(listree_tests listree_tests.cpp deep_copy_test.cpp cursor_traversal_test.cpp traverse_test.cpp cycle_test.cpp ../core/cursor.cpp ../core/debug.cpp)
target_include_directories(listree_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(listree_tests PRIVATE listree gtest gtest_main pthread)
add_test(NAME ListreeTests COMMAND listree_tests)

add_executable(cursor_tail_test cursor_tail_test.cpp ../core/cursor.cpp ../core/debug.cpp)
target_include_directories(cursor_tail_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cursor_tail_test PRIVATE listree pthread)
add_test(NAME CursorTailTest COMMAND cursor_tail_test)
