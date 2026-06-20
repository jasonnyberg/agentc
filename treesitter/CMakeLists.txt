cmake_minimum_required(VERSION 3.10)
project(agentc_treesitter)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find tree-sitter library
find_library(TREE_SITTER_LIB tree-sitter)
find_path(TREE_SITTER_INCLUDE_DIR tree_sitter/api.h)

if(NOT TREE_SITTER_LIB OR NOT TREE_SITTER_INCLUDE_DIR)
    message(FATAL_ERROR "libtree-sitter not found. Install libtree-sitter-devel.")
endif()

# Build the Edict tree-sitter parser as a shared library
add_library(tree_sitter_edict SHARED edict/src/parser.c)
target_include_directories(tree_sitter_edict PUBLIC ${TREE_SITTER_INCLUDE_DIR})
set_target_properties(tree_sitter_edict PROPERTIES
    OUTPUT_NAME "tree-sitter-edict"
    POSITION_INDEPENDENT_CODE ON
)

# Build the C++ bridge library
add_library(agentc_treesitter SHARED tree_sitter_bridge.cpp)
target_include_directories(agentc_treesitter
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${TREE_SITTER_INCLUDE_DIR}
)
target_link_libraries(agentc_treesitter
    PUBLIC
    listree
    reflect
    ${TREE_SITTER_LIB}
    dl
)

# Tests
enable_testing()
add_executable(treesitter_tests
    tests/treesitter_test.cpp
)
target_include_directories(treesitter_tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${TREE_SITTER_INCLUDE_DIR}
)
target_compile_definitions(treesitter_tests PRIVATE
    TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(treesitter_tests
    agentc_treesitter
    tree_sitter_edict
    listree
    reflect
    ${TREE_SITTER_LIB}
    gtest
    gtest_main
    pthread
    dl
)
add_test(NAME treesitter_tests COMMAND treesitter_tests)
