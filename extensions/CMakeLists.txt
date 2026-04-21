cmake_minimum_required(VERSION 3.10)
project(AgentCExtensions)

add_library(agentc_extensions SHARED agentc_stdlib.cpp)
target_include_directories(agentc_extensions PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(agentc_extensions PUBLIC cartographer)
