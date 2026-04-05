cmake_minimum_required(VERSION 3.10)
project(AgentCDemos)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AGENTC_WITH_SDL_DEMO "Build the optional SDL3-backed AgentC demo" OFF)
set(AGENTC_SDL3_ROOT "" CACHE PATH "Path to an SDL3 install prefix")

include_directories(${CMAKE_SOURCE_DIR})

add_executable(demo_capabilities demo_capabilities.cpp)
target_link_libraries(demo_capabilities listree reflect cartographer kanren libedict)

add_executable(demo_speculation demo_speculation.cpp)
target_link_libraries(demo_speculation libedict cartographer pthread)

add_executable(demo_kanren_reasoning demo_kanren_reasoning.cpp)
target_link_libraries(demo_kanren_reasoning kanren listree pthread)

add_executable(demo_kanren_edict demo_kanren_edict.cpp)
target_link_libraries(demo_kanren_edict libedict cartographer kanren pthread)

add_executable(demo_rewrite_runtime demo_rewrite_runtime.cpp)
target_link_libraries(demo_rewrite_runtime libedict cartographer pthread)

add_executable(demo_rewrite_language demo_rewrite_language.cpp)
target_link_libraries(demo_rewrite_language libedict cartographer kanren pthread)

add_executable(demo_cognitive_core_validation demo_cognitive_core_validation.cpp)
target_link_libraries(demo_cognitive_core_validation libedict cartographer kanren pthread)

add_executable(demo_arena_metadata_persistence demo_arena_metadata_persistence.cpp)
target_link_libraries(demo_arena_metadata_persistence reflect pthread)

add_executable(demo_ffi_libc demo_ffi_libc.cpp)
target_link_libraries(demo_ffi_libc listree reflect cartographer pthread)

if(AGENTC_WITH_SDL_DEMO)
  if(NOT AGENTC_SDL3_ROOT AND EXISTS "/home/jwnyberg/SDL3/SDL3-3.4.4/install")
    set(AGENTC_SDL3_ROOT "/home/jwnyberg/SDL3/SDL3-3.4.4/install")
  endif()

  if(AGENTC_SDL3_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${AGENTC_SDL3_ROOT}")
  endif()

  find_package(SDL3 CONFIG REQUIRED)

  add_library(agentc_sdl_triangle_demo SHARED demo_sdl_triangle_bridge.cpp)
  target_link_libraries(agentc_sdl_triangle_demo PRIVATE SDL3::SDL3)

  if(AGENTC_SDL3_ROOT)
    set_target_properties(agentc_sdl_triangle_demo PROPERTIES
      BUILD_RPATH "${AGENTC_SDL3_ROOT}/lib64;${AGENTC_SDL3_ROOT}/lib"
      INSTALL_RPATH "${AGENTC_SDL3_ROOT}/lib64;${AGENTC_SDL3_ROOT}/lib")
  endif()
endif()
