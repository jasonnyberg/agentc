cmake_minimum_required(VERSION 3.10)
project(cartographer)

find_library(LIBCLANG_LIBRARY NAMES clang libclang)
find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBFFI REQUIRED libffi)

add_library(cartographer SHARED mapper.cpp ffi.cpp boxing.cpp ltv_api.cpp boxing_export.cpp parser.cpp protocol.cpp resolver.cpp service.cpp tool_cli.cpp)
target_include_directories(cartographer PRIVATE ${LIBCLANG_INCLUDE_DIR} ${LIBFFI_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cartographer PUBLIC reflect PRIVATE ${LIBCLANG_LIBRARY} ${LIBFFI_LIBRARIES} dl)

add_library(agentmath_poc SHARED tests/libagentmath_poc.c)
add_library(agentthreads_poc SHARED tests/libagentthreads_poc.cpp)
target_link_libraries(agentthreads_poc PRIVATE cartographer pthread)

add_executable(cartographer_tests tests/mapper_tests.cpp tests/ffi_tests.cpp tests/poc_test.cpp tests/service_tests.cpp tests/tool_tests.cpp tests/ltv_api_tests.cpp)
target_link_libraries(cartographer_tests PRIVATE cartographer reflect agentthreads_poc ${LIBCLANG_LIBRARY} ${LIBFFI_LIBRARIES} dl gtest gtest_main pthread)
target_compile_definitions(cartographer_tests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
    TEST_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
add_test(NAME cartographer_tests COMMAND cartographer_tests)

add_executable(cartographer_parse cartographer_parse.cpp)
target_link_libraries(cartographer_parse PRIVATE cartographer)

add_executable(cartographer_resolve cartographer_resolve.cpp)
target_link_libraries(cartographer_resolve PRIVATE cartographer)

add_executable(cartographer_dump cartographer_dump.cpp)
target_include_directories(cartographer_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cartographer_dump PRIVATE cartographer)

add_executable(cartographer_schema_inspect cartographer_schema_inspect.cpp)
target_include_directories(cartographer_schema_inspect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cartographer_schema_inspect PRIVATE cartographer)

add_executable(cartographer_resolve_inspect cartographer_resolve_inspect.cpp)
target_include_directories(cartographer_resolve_inspect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(cartographer_resolve_inspect PRIVATE cartographer)

add_executable(agentc_visualize visualizer.cpp)
target_include_directories(agentc_visualize PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(agentc_visualize PRIVATE cartographer reflect ${LIBCLANG_LIBRARY})
