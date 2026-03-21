# Dashboard

**Project**: AgentC / J3 (transitional name — also called AgentLang)  
**Primary Goal**: G001 — Codebase Review: Optimization and Redundancy/Inconsistency Analysis  
**Last Updated**: 2026-03-20

---

## Current Focus

**Active Goals**: G018 — FFI LTV Passthrough (Hoist Boxing to Pure FFI)
**Status**: G002–G013 Complete. G014 Cancelled. G016–G017 Complete. G018 Active. Namespace migration `j3` → `agentc` complete. Directory restructure complete. **7/7 test suites pass (74/74 tests).**
**Last Updated**: 2026-03-20

**Active Task**: G018 — FFI LTV Passthrough — Hoist `VMOP_BOX`/`VMOP_UNBOX`/`VMOP_BOX_FREE` out of the VM into a C-ABI core LTV library + `libboxing.so`. Extend `convertValue`/`convertReturn` for `LTV*` passthrough. Remove boxing opcodes from VM. 🔗[G018 index](./Knowledge/Goals/G018-FfiLtvPassthrough/index.md)

**Completed Task**: G017 — Edict Stdin/File Script Mode (2026-03-20) — Added `EdictREPL::runScript(std::istream&)`; wired `edict -` (stdin) and `edict FILE` in `main.cpp`. Comments (`#`), blank lines, `\r` stripping supported. Build clean; 7/7 suites pass.

**Completed Task**: G016 — LMDB Optional Compile-Time Build (2026-03-20) — Added `AGENTC_WITH_LMDB` CMake option (default OFF). Guarded all LMDB code in `core/alloc.h`, `core/alloc.cpp`, `tests/alloc_tests.cpp`, `demo/demo_arena_metadata_persistence.cpp`. Build clean; 7/7 suites pass.

**Completed Task**: RegressionMatrixTest fix (2026-03-18) — Root cause: quadratic VM construction (65² pairs × ~18ms/VM = ~76s). Fix: hoist single `EdictVM vm`, use `beginTransaction()`/`rollbackTransaction()` for O(1) reset, skip 16 heavy I/O opcodes via `isHeavyOp()`. Result: edict_tests 1.1s (was >60s timeout). All 7 suites pass.
**Completed Task**: Directory restructure (2026-03-18) — top-level source files moved to `core/`, `tst/` split into `tests/` (test files) + `demo/` (demo programs). All includes updated. `demo_math.h` moved to `demo/`.
**Completed Task**: Namespace migration `j3` → `agentc` (2026-03-18) — all namespaces renamed: `j3` → `agentc`, `j3::log` → `agentc::log`, `j3::cartographer` → `agentc::cartographer`, `j3::kanren` → `agentc::kanren`, `namespace edict` → `namespace agentc::edict`. CMake: `project(J3)` → `project(AgentC)`, `j3visualize` → `agentc_visualize`. Deleted stale `edict/Makefile`.
**Completed Task**: gendocs — `scripts/gendocs.py` + `make gendocs` target; produces `design/` with L1/L2/L3 HTML (897 fns, 7 components) (2026-03-16)
**Completed Task**: G015(implicit) — int/double type removal from edict VM/type system (2026-03-16, 74/74 tests pass)
**Completed Task**: WP002 — Edict quote handling investigation (2026-03-16)
**Completed Task**: G014 plan written — quote elimination goal and tasks documented (2026-03-16)
**Completed Task**: G004(C3) — ListreeValue heap elimination COMPLETE (commit: 5c0bab1)
**Completed Task**: G013 — Code Hygiene & Cleanup (2026-03-16)
**Completed Task**: demo_capabilities.cpp build fix (2026-03-16)

**Accomplishments**:
- ✅ G017: Edict stdin/file script mode — `runScript(istream&)`; `edict -` and `edict FILE` CLI modes; `#` comments; 7/7 suites pass (2026-03-20)
- ✅ G016: LMDB optional compile-time build — `AGENTC_WITH_LMDB` CMake option (default OFF); guards in 5 files; 7/7 suites pass (2026-03-20)
- ✅ G001: Full codebase review completed, producing `doc/code-review.md` (32 issues).
- ✅ G002 (C1, C2, C6): Fixed ODR in `alloc.h`, buffer overflow in `TraversalContext`, verified `cursor.cpp::up()`.
- ✅ G003 (C4, H8): Verified `service.cpp` mutex, made `mapper.cpp` anonCount atomic.
- ✅ G005 (C5, H5, M1): Audited dispatch table, added static_assert, documented op_CALL, replaced magic bytes with `VMPushextType`.
- ✅ G004 (C3+M8) — Heap-in-Slab Violations — **Complete** (2026-03-16)
- ✅ G004 (C3): ListreeValue string/binary heap elimination — SSO, BlobAllocator, StaticView, transaction integration complete; 73/73 edict tests pass (excl. RegressionMatrixTest which hangs).
- ✅ G006 (H1): Documented speculation O(N) deep-copy, verified `TransactionTest`.
- ✅ G007 (H2, H3): Fixed FFI multi-handle loading, added stack buffers to eliminate malloc for ≤16 args, fixed pointer return types.
- ✅ G008 (H6, M7): Re-implemented Cursor `next()`/`prev()` via in-order tree traversal, fixed `down()` lexicographic fallback.
- ✅ G009 (H7, M2, M3, M6): Wired REPL `processLine`, implemented `registerCursorOperations`, tracked source locations in Tokenizer, refactored `op_EVAL`.
- ✅ G010 (M4): Implemented `snooze()` for Mini-Kanren to convert C-stack recursion to lazy heap recursion; fixed `membero`/`appendo`.
- ✅ G011 (M5): Added robust FFI system call blocklist (`PROC/FILE/NET/DL/MEM/SIG`) using `std::unordered_set`.
- ✅ G012 (M10, M11, L5): Fixed `make repl` target, pinned GoogleTest to v1.14.0, fixed CMake caching error.
- ✅ G013 (L1-L7, M9): Archived legacy files, added per-module logging, documented `LtvFlags`, fixed useless stubs.
- ✅ Build Fix: Updated `demo_capabilities.cpp` to use the new `StateStream::next()` iterator API from G010, restoring full build stability.

---

### Active Goals
- 🔗[G018 — FFI LTV Passthrough](./Knowledge/Goals/G018-FfiLtvPassthrough/index.md) — Active: hoist boxing/unboxing from VM opcodes into C-ABI `libboxing.so` + `libagentc_core.so`; extend FFI `LTV*` passthrough; remove `VMOP_BOX`/`VMOP_UNBOX`/`VMOP_BOX_FREE`

### Recommended Next Steps
1. **LMDB Integration**: G016+G017 complete — can proceed with LMDB persistence goals (G040–G042).
2. **Commit**: All recent changes (directory restructure, RegressionMatrixTest fix, G016, G017) are uncommitted.

### Completed Goals
- ✅ G017 — Edict Stdin/File Script Mode (2026-03-20) — `runScript(istream&)`; `edict -` stdin and `edict FILE` CLI modes; `#` comments; 74/74 tests pass
- ✅ G016 — LMDB Optional Compile-Time Build (2026-03-20) — `AGENTC_WITH_LMDB` CMake option (default OFF); guards in `core/alloc.h`, `core/alloc.cpp`, `tests/alloc_tests.cpp`, `demo/demo_arena_metadata_persistence.cpp`, `CMakeLists.txt`; 74/74 tests pass
- ✅ G001 (partial) — Codebase review completed; issue catalog + recommendations produced (2026-03-16)
- ✅ G002 — Critical UB & ODR Fixes (2026-03-16)
- ✅ G003 — Thread Safety (2026-03-16)
- ✅ G004 — Heap-in-Slab Violations (2026-03-16) — M8: IP stored inline; C3: SSO/BlobAllocator/StaticView + transaction integration; 73/73 edict tests pass
- ✅ G005 — VM Dispatch & Opcodes (2026-03-16)
- ✅ G006 — Speculation & Transactions (2026-03-16) — beginTransaction() deep-copy necessity documented; op_SPECULATE separate-VM approach documented; TransactionTest 6/6 pass
- ✅ G007 — FFI Correctness (2026-03-16) — H2: multi-handle map with realpath dedup; H3: pointer return type in convertReturn(); G007.3: stack buffers eliminate malloc for ≤16 args
- ✅ G008 — Cursor & Navigation (2026-03-16) — H6: next()/prev() use forEachTree in-order traversal (not nextName.back()++); M7: down() descends to first lexicographic child (removed hardcoded heuristic)
- ✅ G009 — REPL & Compiler Quality (2026-03-16) — processLine wired; Token source locations tracked; op_EVAL refactored; prelude cached
- ✅ G010 — Mini-Kanren Correctness (2026-03-16) — snooze() added for lazy evaluation; infinite loops in membero/appendo resolved
- ✅ G011 — Security & Safety (2026-03-16) — FFI system calls blocklist added (PROC/FILE/NET/DL etc.) using unordered_set
- ✅ G012 — Build System & Test Infra (2026-03-16) — make repl points to edict; GoogleTest pinned to 1.14.0; cll_test.cc documented
- ✅ G013 — Code Hygiene & Cleanup (2026-03-16) — L1-L4: legacy files archived/removed; L6: per-module logging; M9: LtvFlags documented; L7: namespace rename deferred

---

## Active Agents

### Supervisor Agents
None currently

### Worker Agents
None currently

---

## Knowledge Inventory

Complete index of LOCAL knowledge. Load items relevant to your current task.

### Goals
- G001 — Codebase Review — Active (parent)
- G002 — Critical UB & ODR Fixes — **Complete**
- G003 — Thread Safety — **Complete**
- G004 — Heap-in-Slab Violations — **In Progress** (C3 partial)
- G005 — VM Dispatch & Opcodes — **Complete**
- G006 — Speculation & Transactions — Pending
- G007 — FFI Correctness — Pending
- G008 — Cursor & Navigation — **Complete**
- G009 — REPL & Compiler Quality — **Complete**
- G010 — Mini-Kanren Correctness — **Complete**
- G011 — Security & Safety — **Complete**
- G012 — Build System & Test Infra — **Complete**
- G013 — Code Hygiene & Cleanup — **Complete**
        - G016 — LMDB Optional Compile-Time Build — **Complete** 🔗[index](./Knowledge/Goals/G016-LmdbOptionalBuild/index.md)
        - G017 — Edict Stdin/File Script Mode — **Complete** 🔗[index](./Knowledge/Goals/G017-EdictScriptMode/index.md)
        - G018 — FFI LTV Passthrough — **Active** 🔗[index](./Knowledge/Goals/G018-FfiLtvPassthrough/index.md)

### Facts
(none yet)

### Concepts
(none yet)

### Procedures
(none yet)

### WorkProducts
- WP001 — CodebaseReview: Full issue catalog, architectural analysis, 20 prioritized recommendations (2026-03-16)
- WP002 — EdictQuoteHandling: Investigation of `"` semantics in edict compiler/VM; findings for G014 (2026-03-16)

---

## Timeline Highlights

### Recent Events (Last 7 Days)
- 🔗[2026-03-20: G017 Edict Script Mode + G016 LMDB Optional Build](./Knowledge/Timeline/2026/03/20/index.md) — `runScript(istream&)`, `edict -`/`edict FILE` CLI modes; `AGENTC_WITH_LMDB` CMake option; 74/74 tests pass
- 🔗[2026-03-16: HRM Bootstrap + Codebase Review](./Knowledge/Timeline/2026/03/16/0000-0200/index.md) — Initial HRM structure; full code review; G002–G013 created
- 🔗[2026-03-16: Phase 1 and 2 Complete](./Knowledge/Timeline/2026/03/16/2200-2300/index.md) — Final build fix (`demo_capabilities.cpp`) and project wrap-up; all goals completed or deferred.

### Current Phase
Phase 3: Active — G016 and G017 complete. LMDB persistence goals (G040–G042) ready to proceed.

---

## Blockers & Issues

- **RegressionMatrixTest.PairwiseCoverage** — **RESOLVED** (2026-03-18). Was quadratic VM construction; fixed via single-VM + transaction checkpoint pattern. All 74 tests now pass in 2.44s.
- None blocking Phase 3 start.

---

## Future Enhancements

Items from AgentLang.md Appendix A still aspirational / not implemented:
- Shared-arena sidecar sub-agent handoff (requires G004 + G006 complete)
- Cartographer as general capability service (needs G007 + mature service protocol)
- Mini-Kanren as full planner (needs G010 + expanded surface syntax + fair search)
- Term rewriting as self-optimizing compiler (currently narrower than described)
- Full dot-navigation language ergonomics (needs G008 complete)

Pre-existing LMDB goals (in `Knowledge/Goals/`, not LocalContext):
- G040 — LMDB Persistent Arena Integration
- G041 — Persistent Slab Image Persistence
- G042 — Persistent VM Root State And Restore Validation
These depend on G004 (heap-in-slab) being complete first.

---

## Context Health

**Context Window Usage**: ~40%  
**Knowledge Items Loaded**: 14 (G001–G013, WP001, Dashboard, Timeline)  
**Active Background Tasks**: 0

**Attention Loop Status**: Normal

---

## Notes

- The `Knowledge/Goals/` directory that already existed in the repo (not LocalContext) tracks G040–G042 (LMDB persistence). These depend on G004.
- LSP errors on `cll.h`, `cll.cc`, `juncture.h/cc`, `cll_test.cc` are pre-existing and documented as issues L2, L4 (G013 scope).
- `alloc.h` ODR violation (C1) was fixed in G002.
