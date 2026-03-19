cmake_minimum_required(VERSION 3.10)
project(cartographer)

find_library(LIBCLANG_LIBRARY NAMES clang libclang)
find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBFFI REQUIRED libffi)

add_library(cartographer SHARED mapper.cpp ffi.cpp parser.cpp protocol.cpp resolver.cpp service.cpp tool_cli.cpp ../core/cursor.cpp ../core/debug.cpp)
target_include_directories(cartographer PRIVATE ${LIBCLANG_INCLUDE_DIR} ${LIBFFI_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cartographer PRIVATE ${LIBCLANG_LIBRARY} ${LIBFFI_LIBRARIES} dl listree)

add_library(agentmath_poc SHARED tests/libagentmath_poc.c)

add_executable(cartographer_tests tests/mapper_tests.cpp tests/ffi_tests.cpp tests/poc_test.cpp tests/service_tests.cpp tests/tool_tests.cpp mapper.cpp ffi.cpp parser.cpp protocol.cpp resolver.cpp service.cpp tool_cli.cpp ../core/cursor.cpp ../core/debug.cpp)
target_link_libraries(cartographer_tests PRIVATE ${LIBCLANG_LIBRARY} ${LIBFFI_LIBRARIES} dl listree gtest gtest_main pthread)
target_compile_definitions(cartographer_tests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
    TEST_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME cartographer_tests COMMAND cartographer_tests)

add_executable(cartographer_parse cartographer_parse.cpp)
target_link_libraries(cartographer_parse PRIVATE cartographer)

add_executable(cartographer_resolve cartographer_resolve.cpp)
target_link_libraries(cartographer_resolve PRIVATE cartographer)

add_executable(agentc_visualize visualizer.cpp mapper.cpp ../core/cursor.cpp ../core/debug.cpp ../listree/listree.cpp)
target_link_libraries(agentc_visualize PRIVATE ${LIBCLANG_LIBRARY} listree)
