cmake_minimum_required(VERSION 3.10)
project(edict)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# Library source files
set(LIB_SOURCES
    edict_types.cpp
    edict_compiler.cpp
    edict_vm.cpp
    edict_repl.cpp
)

# Create library
add_library(libedict SHARED ${LIB_SOURCES})
set_target_properties(libedict PROPERTIES OUTPUT_NAME "edict")
target_compile_options(libedict PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(libedict PRIVATE pthread PRIVATE cartographer PUBLIC reflect)

# Create executable
add_executable(edict main.cpp)
target_link_libraries(edict libedict)

# Add tests
enable_testing()
add_executable(edict_tests
    tests/edict_tests.cpp
    tests/vm_execution_test.cpp
    tests/vm_stack_tests.cpp
    tests/regression_matrix.cpp
    tests/repro_hang.cpp
    tests/call_isolation_test.cpp
    tests/callback_test.cpp
    tests/transaction_test.cpp
    tests/rewrite_runtime_test.cpp
    tests/logic_surface_test.cpp
    tests/rewrite_language_test.cpp
    tests/cognitive_validation_test.cpp
    tests/simple_assign_test.cpp
)
target_compile_definitions(edict_tests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../cartographer/tests"
    TEST_BUILD_DIR="${CMAKE_BINARY_DIR}/cartographer"
    TEST_EDICT_BIN_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(edict_tests libedict cartographer kanren gtest gtest_main pthread)
add_test(NAME edict_tests COMMAND edict_tests)

add_executable(splice_test tests/splice_test.cpp)
target_link_libraries(splice_test libedict cartographer kanren pthread)
add_test(NAME splice_test COMMAND splice_test)
