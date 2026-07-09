cmake_minimum_required(VERSION 3.10)
project(agentc_markethub)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# G117 separation constraint: this module consumes AgentC facilities but is
# never linked into core AgentC targets. Dependency direction is one-way:
# markethub -> AgentC. Core targets (libedict, edict, edict_tests, ...) must
# not reference anything defined here.
# ---------------------------------------------------------------------------

# --- Hub library: standalone, std-only -------------------------------------
add_library(markethub STATIC
    src/market_event.cpp
    src/market_hub.cpp
)
target_include_directories(markethub PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(markethub PRIVATE -Wall -Wextra -Werror)
target_link_libraries(markethub PRIVATE pthread)

# --- Phase 1 transitional façade over DeltaGUI/GreekScope GEX analytics ----
# Compiled directly from the sibling DeltaGUI tree (read-only; never modified).
# Built only when that tree is present so AgentC builds stay self-contained.
set(AGENTC_MARKETHUB_DELTAGUI_ROOT "$ENV{HOME}/DeltaGUI/backend_cpp" CACHE PATH
    "DeltaGUI backend_cpp root used to build the GreekScope GEX facade")

set(AGENTC_MARKETHUB_HAVE_FACADE 0)
if(EXISTS "${AGENTC_MARKETHUB_DELTAGUI_ROOT}/src/analytics_service.cpp")
    set(AGENTC_MARKETHUB_HAVE_FACADE 1)
    add_library(agentc_greekscope_facade SHARED
        facade/greekscope_facade.cpp
        ${AGENTC_MARKETHUB_DELTAGUI_ROOT}/src/analytics_service.cpp
        ${AGENTC_MARKETHUB_DELTAGUI_ROOT}/src/gex/gex_math.cpp
        ${AGENTC_MARKETHUB_DELTAGUI_ROOT}/src/gex/gex_book.cpp
        ${AGENTC_MARKETHUB_DELTAGUI_ROOT}/src/gex/gex_calculator.cpp
        ${AGENTC_MARKETHUB_DELTAGUI_ROOT}/src/gex/gex_json.cpp
    )
    target_include_directories(agentc_greekscope_facade PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/facade
        ${AGENTC_MARKETHUB_DELTAGUI_ROOT}/include
    )
    # Match the DeltaGUI project's own language level and warning posture
    # (external sources are compiled verbatim; no -Werror).
    set_target_properties(agentc_greekscope_facade PROPERTIES CXX_STANDARD 20)
    target_compile_options(agentc_greekscope_facade PRIVATE -Wall -Wextra)
    message(STATUS "markethub: GreekScope facade enabled from ${AGENTC_MARKETHUB_DELTAGUI_ROOT}")
else()
    message(STATUS "markethub: DeltaGUI tree not found; GreekScope facade disabled")
endif()

# --- Tests: the AgentC-consumer side ----------------------------------------
# markethub_tests links libedict to drive the G116 TCC allowlist + isolated
# worker execution (bridge demo). This is the one-way consumption boundary.
# AGENTC_TCC_INCLUDE_DIR / AGENTC_TCC_LIBRARY are cache variables populated by
# edict/CMakeLists.txt discovery (top-level adds edict before markethub).
set(MARKETHUB_HAVE_TCC 0)
if(AGENTC_ENABLE_TCC AND AGENTC_TCC_INCLUDE_DIR AND AGENTC_TCC_LIBRARY)
    set(MARKETHUB_HAVE_TCC 1)
endif()

add_executable(markethub_tests
    tests/market_hub_test.cpp
    tests/facade_bridge_test.cpp
)
target_compile_definitions(markethub_tests PRIVATE
    MARKETHUB_HAVE_TCC=${MARKETHUB_HAVE_TCC}
    MARKETHUB_HAVE_FACADE=${AGENTC_MARKETHUB_HAVE_FACADE}
    MARKETHUB_FACADE_LIBRARY="${CMAKE_CURRENT_BINARY_DIR}/libagentc_greekscope_facade.so"
)
target_include_directories(markethub_tests PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(markethub_tests markethub libedict gtest gtest_main pthread ${CMAKE_DL_LIBS})
if(AGENTC_MARKETHUB_HAVE_FACADE)
    add_dependencies(markethub_tests agentc_greekscope_facade)
endif()
if(MARKETHUB_HAVE_TCC)
    add_dependencies(markethub_tests agentc_tcc_worker_exec)
endif()
add_test(NAME markethub_tests COMMAND markethub_tests)
