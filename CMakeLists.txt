cmake_minimum_required(VERSION 3.10)
project(AgentC)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

include(FetchContent)
# M11: Pin GoogleTest to v1.14.0 with a content hash for reproducible builds.
# Previously fetched from the floating `main` branch (non-reproducible, offline-unsafe).
# SHA256 verified against the official GitHub release tarball.
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Add listree subdirectory first so it's built before reflect
add_subdirectory(listree)

# Source files for the core library (arena allocator, containers, cursor, debug)
set(REFLECT_SOURCES
    core/alloc.cpp
    core/cursor.cpp
    core/debug.cpp
)

# Create the library
add_library(reflect SHARED ${REFLECT_SOURCES})

# Expose core/ as an include path so consumers can use "core/alloc.h" etc.
target_include_directories(reflect
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against listree
target_link_libraries(reflect
    PUBLIC
    listree
    ${CMAKE_DL_LIBS}
)

# Add tests
add_subdirectory(tests)

# Add demos
add_subdirectory(demo)

# Add J3 Interpreter (experimental VM)
# add_subdirectory(interpreter)

# Add Cartographer (Header Mapping)
add_subdirectory(cartographer)

# Add Edict interpreter
add_subdirectory(edict)

# Add Mini-Kanren
add_subdirectory(kanren)

