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

option(AGENTC_ENABLE_TCC "Enable TinyCC/libtcc support when available" ON)
set(AGENTC_TCC_ROOT "$ENV{AGENTC_TCC_ROOT}" CACHE PATH
    "Optional TinyCC source/build/install root containing libtcc.h and libtcc")

set(_AGENTC_TCC_DISCOVERY_FINGERPRINT "${AGENTC_ENABLE_TCC};${AGENTC_TCC_ROOT}")
if(DEFINED AGENTC_TCC_DISCOVERY_STATE AND
   NOT "${AGENTC_TCC_DISCOVERY_STATE}" STREQUAL "${_AGENTC_TCC_DISCOVERY_FINGERPRINT}")
    unset(AGENTC_TCC_INCLUDE_DIR CACHE)
    unset(AGENTC_TCC_LIBRARY CACHE)
endif()
set(AGENTC_TCC_DISCOVERY_STATE "${_AGENTC_TCC_DISCOVERY_FINGERPRINT}" CACHE INTERNAL
    "Tracks the TinyCC discovery inputs used for the current cache")

set(AGENTC_HAVE_TCC 0)
set(_AGENTC_TCC_HINT_INCLUDE_DIRS)
set(_AGENTC_TCC_HINT_LIBRARY_DIRS)
if(AGENTC_TCC_ROOT)
    if(DEFINED AGENTC_TCC_INCLUDE_DIR AND
       NOT "${AGENTC_TCC_INCLUDE_DIR}" MATCHES "^${AGENTC_TCC_ROOT}(/|$)")
        unset(AGENTC_TCC_INCLUDE_DIR CACHE)
    endif()
    if(DEFINED AGENTC_TCC_LIBRARY AND
       NOT "${AGENTC_TCC_LIBRARY}" MATCHES "^${AGENTC_TCC_ROOT}(/|$)")
        unset(AGENTC_TCC_LIBRARY CACHE)
    endif()
    list(APPEND _AGENTC_TCC_HINT_INCLUDE_DIRS
        "${AGENTC_TCC_ROOT}"
        "${AGENTC_TCC_ROOT}/include")
    list(APPEND _AGENTC_TCC_HINT_LIBRARY_DIRS
        "${AGENTC_TCC_ROOT}"
        "${AGENTC_TCC_ROOT}/lib"
        "${AGENTC_TCC_ROOT}/lib64")
endif()

if(AGENTC_ENABLE_TCC)
    find_path(AGENTC_TCC_INCLUDE_DIR libtcc.h
        HINTS ${_AGENTC_TCC_HINT_INCLUDE_DIRS}
        PATHS /usr/include /usr/local/include)
    find_library(AGENTC_TCC_LIBRARY NAMES tcc libtcc
        HINTS ${_AGENTC_TCC_HINT_LIBRARY_DIRS}
        PATHS /usr/lib64 /usr/lib /usr/local/lib /usr/local/lib64)
    if(AGENTC_TCC_INCLUDE_DIR AND AGENTC_TCC_LIBRARY)
        set(AGENTC_HAVE_TCC 1)
        message(STATUS "TinyCC support enabled with ${AGENTC_TCC_LIBRARY}")
    else()
        message(STATUS "TinyCC support requested but libtcc was not found; building without TCC")
    endif()
else()
    message(STATUS "TinyCC support disabled via AGENTC_ENABLE_TCC=OFF")
endif()

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
    root1_await_scheduler.cpp
    static_declaration_image.cpp
    static_slot_table_image.cpp
    edict_repl.cpp
    block_accumulator.cpp
    edict_vm_treesitter.cpp
    edict_vm_knowledge_graph.cpp
    edict_vm_overlay.cpp
    edict_vm_tcc.cpp
    tcc_runtime.cpp
)

# Create library
add_library(libedict SHARED ${LIB_SOURCES})
set_target_properties(libedict PROPERTIES OUTPUT_NAME "edict")
target_compile_options(libedict PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
target_link_libraries(libedict PRIVATE pthread PRIVATE cartographer PUBLIC reflect)
target_link_libraries(libedict PUBLIC agentc_treesitter)
target_compile_definitions(libedict PRIVATE
    AGENTC_HAVE_TCC=${AGENTC_HAVE_TCC}
    AGENTC_TCC_API_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    AGENTC_TCC_WORKER_EXECUTABLE="${CMAKE_CURRENT_BINARY_DIR}/agentc_tcc_worker_exec"
)

# Create executable
add_executable(edict
    main.cpp
    ../cpp-agent/runtime/persistence/session_image_store.cpp
    ../cpp-agent/runtime/persistence/session_state_store.cpp
)
target_include_directories(edict PRIVATE ${nlohmann_json_SOURCE_DIR})
target_link_libraries(edict libedict)

add_executable(edict_worker_exec worker_exec_main.cpp)
target_link_libraries(edict_worker_exec libedict)

if(AGENTC_HAVE_TCC)
    set(AGENTC_TCC_LIBRARY_ROOT "${AGENTC_TCC_ROOT}")
    add_executable(agentc_tcc_worker_exec tcc_worker_exec_main.cpp tcc_worker_native.cpp)
    target_include_directories(agentc_tcc_worker_exec PRIVATE ${AGENTC_TCC_INCLUDE_DIR})
    target_compile_definitions(agentc_tcc_worker_exec PRIVATE
        AGENTC_TCC_API_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        AGENTC_TCC_LIBRARY_ROOT="${AGENTC_TCC_LIBRARY_ROOT}"
    )
    target_link_libraries(agentc_tcc_worker_exec PRIVATE libedict ${AGENTC_TCC_LIBRARY} ${CMAKE_DL_LIBS} m)

    add_library(agentc_tcc_test_adapter SHARED tests/tcc_test_adapter.cpp)
    set_target_properties(agentc_tcc_test_adapter PROPERTIES OUTPUT_NAME "agentc_tcc_test_adapter")
endif()

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
    tests/root1_await_scheduler_test.cpp
    tests/static_declaration_image_test.cpp
    tests/static_slot_table_image_test.cpp
    tests/treesitter_edict_test.cpp
    tests/cognitive_scaffold_test.cpp
    tests/overlay_dictionary_test.cpp
    tests/tcc_runtime_test.cpp
)
target_compile_definitions(edict_tests PRIVATE
    TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../cartographer/tests"
    TEST_BUILD_DIR="${CMAKE_BINARY_DIR}/cartographer"
    TEST_EDICT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    TEST_EDICT_BIN_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    TEST_ROOT_BUILD_DIR="${CMAKE_BINARY_DIR}"
    AGENTC_HAVE_TCC=${AGENTC_HAVE_TCC}
    TEST_TCC_ADAPTER_PATH="${CMAKE_CURRENT_BINARY_DIR}/libagentc_tcc_test_adapter.so"
)
target_link_libraries(edict_tests libedict cartographer kanren gtest gtest_main pthread)
add_dependencies(edict_tests edict edict_worker_exec agentmath_poc agentthreads_poc tree_sitter_edict)
if(AGENTC_HAVE_TCC)
    add_dependencies(edict_tests agentc_tcc_worker_exec agentc_tcc_test_adapter)
endif()
add_test(NAME edict_tests COMMAND edict_tests)

add_executable(splice_test tests/splice_test.cpp)
target_link_libraries(splice_test libedict cartographer kanren pthread)
add_test(NAME splice_test COMMAND splice_test)
