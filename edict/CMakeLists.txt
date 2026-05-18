cmake_minimum_required(VERSION 3.10)
project(edict)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(nlohmann_json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp DOWNLOAD_NO_EXTRACT TRUE)
FetchContent_MakeAvailable(nlohmann_json)
file(COPY ${nlohmann_json_SOURCE_DIR}/json.hpp DESTINATION ${nlohmann_json_SOURCE_DIR}/nlohmann/)

# Add include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# Library source files
set(LIB_SOURCES
    edict_types.cpp
    edict_compiler.cpp
    edict_vm.cpp
    edict_vm_core.cpp
    edict_vm_ffi.cpp
    edict_vm_bootstrap.cpp
    edict_worker_runtime.cpp
    edict_worker_primitives.cpp
    edict_worker_primitives_ffi.cpp
    edict_root1_primitives.cpp
    edict_repl.cpp
    block_accumulator.cpp
)

# Create library
add_library(libedict SHARED ${LIB_SOURCES})
set_target_properties(libedict PROPERTIES OUTPUT_NAME "edict")
target_compile_options(libedict PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(libedict PRIVATE pthread PRIVATE cartographer PUBLIC reflect)

# Create executable
add_executable(edict
    main.cpp
    ../cpp-agent/runtime/persistence/session_image_store.cpp
    ../cpp-agent/runtime/persistence/session_state_store.cpp
)
target_include_directories(edict PRIVATE ${nlohmann_json_SOURCE_DIR})
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
    tests/pi_simulation_test.cpp
    tests/session_cli_test.cpp
    tests/intern_worker_test.cpp
    tests/root1_primitive_module_test.cpp
)
target_compile_definitions(edict_tests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../cartographer/tests"
    TEST_BUILD_DIR="${CMAKE_BINARY_DIR}/cartographer"
    TEST_EDICT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    TEST_EDICT_BIN_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)
target_link_libraries(edict_tests libedict cartographer kanren gtest gtest_main pthread)
add_dependencies(edict_tests edict agentmath_poc agentthreads_poc)
add_test(NAME edict_tests COMMAND edict_tests)

add_executable(splice_test tests/splice_test.cpp)
target_link_libraries(splice_test libedict cartographer kanren pthread)
add_test(NAME splice_test COMMAND splice_test)
