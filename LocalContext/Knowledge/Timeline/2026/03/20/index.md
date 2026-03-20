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

---

## Edict Stdin/File Script Mode (G017) — Complete

**Goal**: Add non-interactive script execution to the edict interpreter (stdin pipe and file arguments).

### Changes Made

1. **`edict/edict_repl.h`** — Added `#include <istream>`; added `bool runScript(std::istream& in)` to public API.

2. **`edict/edict_repl.cpp`** — Implemented `runScript`: reads line-by-line, strips `\r`, skips blank lines and `#` comments, compiles+executes each line, prints `Error (line N): <msg>` on failure, returns bool.

3. **`edict/main.cpp`** — Added `#include <fstream>`; updated `printUsage`; wired:
   - `edict -` → `runScript(std::cin)`, exits 0/1
   - `edict FILE` → `runScript(ifstream)`, exits 0/1; error message if file not found
   - Unknown `-` flags → usage + exit 2

### Result

- Build: clean
- Stdin, file, missing-file, and unknown-flag cases all behave correctly
- 7/7 test suites pass, 74/74 tests pass

**Related Goal**: [G017-EdictScriptMode](../../Goals/G017-EdictScriptMode/index.md)
