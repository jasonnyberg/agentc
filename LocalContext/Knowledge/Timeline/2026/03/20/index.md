# Timeline: 2026-03-20

## LMDB Optional Compile-Time Build (G016) — Complete

**Goal**: Make LMDB optional at compile time so AgentC builds and runs on systems without LMDB installed.

### Changes Made

1. **`CMakeLists.txt`** — Added `option(AGENTC_WITH_LMDB "Build with LMDB arena store support" OFF)` and conditional `target_compile_definitions(reflect PUBLIC AGENTC_WITH_LMDB)`.

2. **`core/alloc.h`** — Wrapped entire `LmdbArenaStore` class declaration with `#ifdef AGENTC_WITH_LMDB` / `#endif`.

3. **`core/alloc.cpp`** — Wrapped `LmdbArenaStore::Impl` + all method implementations with `#ifdef AGENTC_WITH_LMDB` / `#endif`.

4. **`tests/alloc_tests.cpp`** — Wrapped all 3 LMDB-specific tests (non-contiguous) with separate `#ifdef`/`#endif` pairs.

5. **`demo/demo_arena_metadata_persistence.cpp`** — Wrapped LMDB demo section (lines 177–229) with `#ifdef AGENTC_WITH_LMDB` / `#endif`.

### Result

- Build: clean (no new errors or warnings)
- Tests: 7/7 suites pass, 74/74 tests pass
- LMDB guard defaults to OFF; enable with `-DAGENTC_WITH_LMDB=ON`

**Related Goal**: [G016-LmdbOptionalBuild](../../Goals/G016-LmdbOptionalBuild/index.md)
