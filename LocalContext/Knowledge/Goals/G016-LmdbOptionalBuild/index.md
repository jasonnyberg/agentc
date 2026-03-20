# G016 — LMDB Optional Compile-Time Build

**Status**: Complete  
**Completed**: 2026-03-20  
**Priority**: Medium  

## Objective

Make LMDB optional at compile time so AgentC builds and runs on systems without LMDB installed. Gate all LMDB-related code behind `#ifdef AGENTC_WITH_LMDB`. Default: OFF.

## Design Decisions

- Guard symbol: `#ifdef AGENTC_WITH_LMDB`
- CMake option: `option(AGENTC_WITH_LMDB "Build with LMDB arena store support" OFF)`
- When ON: `target_compile_definitions(reflect PUBLIC AGENTC_WITH_LMDB)`
- LMDB already had no link-time/header dependency (uses `dlopen`, re-declares MDB types locally) — only source-level guards needed

## Files Changed

| File | Change |
|------|--------|
| `CMakeLists.txt` | Added `option(AGENTC_WITH_LMDB ...)` (default OFF); `target_compile_definitions(reflect PUBLIC AGENTC_WITH_LMDB)` inside `if(AGENTC_WITH_LMDB)` block |
| `core/alloc.h` | Wrapped entire `LmdbArenaStore` class declaration with `#ifdef`/`#endif` |
| `core/alloc.cpp` | Wrapped `LmdbArenaStore::Impl` class and all `LmdbArenaStore::*` method implementations |
| `tests/alloc_tests.cpp` | Wrapped all 3 LMDB-specific tests (non-contiguous) with separate `#ifdef`/`#endif` pairs |
| `demo/demo_arena_metadata_persistence.cpp` | Wrapped LMDB demo section (~lines 177–229) with `#ifdef`/`#endif` |

## Outcome

- Build succeeds with `AGENTC_WITH_LMDB=OFF` (default)
- All 7/7 test suites pass (74/74 tests)
- LMDB tests skipped cleanly when guard is absent
- No new compiler warnings introduced
