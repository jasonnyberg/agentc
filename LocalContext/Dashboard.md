# Dashboard

**Project**: AgentC / J3 (transitional name — also called AgentLang)  
**Primary Goal**: G001 — Codebase Review: Optimization and Redundancy/Inconsistency Analysis  
**Last Updated**: 2026-04-03

## Current Focus

**Active Goals**: G046 (continuation-based speculation implementation), G049 (Edict/VM multithreading implementation), G051 (cursor-visited read-only boundary evaluation)
**Status**: G044 (JSON module cache) complete. G045 (language enhancement review) complete and refined so both the literal-model note and the remaining proposals explicitly preserve Edict's intentional unified literal model. G046 remains in active implementation: `op_SPECULATE()` now runs nested speculative code in the current VM via checkpoint + nested code-frame execution, and follow-up tests now cover rewrite-rule rollback, closure-driven mutation rollback, and nested-speculation isolation. G047 is now complete: the active logic authoring story is canonical object/Listree specs plus imported evaluation, with higher-level sugar expected to live in ordinary Edict wrappers rather than compiler or VM special cases. G048 is complete: the Listree-spec-to-kanren bridge lives in `kanren/logic_evaluator.cpp`, `kanren/runtime_ffi.cpp` exposes both the generalized runtime-handle ABI and direct imported `agentc_logic_eval_ltv(...)`, Edict-side imported-capability coverage exercises that boundary through the normal Cartographer resolve/import path, pure Edict wrapper thunks prove canonical logic specs can be built with ordinary `f(x)` forms and evaluated through imported `libkanren.so`, `VMOP_LOGIC_RUN` has been removed entirely, `libedict` no longer links directly against `kanren`, the main README/demo/reference/fact artifacts now describe the imported-capability model instead of `logic { ... }`, `logic_run !`, or compiler-native `logic(...)`, and older work products/plans now carry explicit historical-status notes where they still discuss superseded logic forms. G049 remains in active implementation: the chosen first multithreading direction is fresh-VM-per-thread execution through imported pthread-backed callback helpers, with explicit protected shared-value cells rather than arbitrary concurrent mutation of live Listree/VM objects. G050 is now complete: helper/test drift is mitigated by target dependencies, cross-thread slab-handle retains are atomic via `Allocator<T>::tryRetain(...)`, repeated focused thread-runtime runs are green, standalone `./build/edict/edict_tests` passes (`86/86`), and `ctest --test-dir build --output-on-failure` passes (`7/7`) after a full rebuild.
**Last Updated**: 2026-04-03

**Active Task**: Continue G049 follow-through after evaluating G051. The protected shared-value boundary has now been re-validated and the auxiliary status-returning helper API has been removed; remaining G049 work is broader cleanup/hardening rather than first-slice model definition.

**Previous Active Task**: G020 — Early Type Binding (`bindTypes()`) — **COMPLETE** (2026-03-21). `bindTypes()` pass added to `Mapper::materialize()`; resolves `"struct X"` field types into `type_def` CPtr children. `ns` param fully removed from `box()`/`unbox()`/`packStruct()`/`unpackStruct()`, C ABI, edict bootstrap, unit tests, and demo/test scripts. New `BindTypesCreatesTypeDef` test in `mapper_tests.cpp`; `Rect` struct added to `test_input.h`.

**Previous Active Task**: G018 Phase D — Remove VM Boxing Opcodes — **COMPLETE** (2026-03-21). `VMOP_BOX/UNBOX/BOX_FREE` and all op_ implementations removed. `createBootstrapCuratedCartographer()` builds func-def trees for `evalDispatchFFI` path. Fixed two bugs: (1) `ffi_type_ltv_handle` size/alignment=0 → 4/4; (2) SlabId ABI encode was `(index<<16)|offset` but x86-64 pair layout is `index|(offset<<16)` — fixed. 🔗[G018 index](./Knowledge/Goals/G018-FfiLtvPassthrough/index.md)

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
- ✅ agentc.sh wrappers: all 7 CLI tools wrapped; `agentc_schema`/`agentc_resolve` pipeline compositions; `agentc_test` (9/9 PASS) + `agentc_demo` meta-functions (2026-03-21)
- ✅ Cartographer CLI pipeline: `cartographer_dump`→`cartographer_parse`→`cartographer_schema_inspect`→`cartographer_resolve`→`cartographer_resolve_inspect`; test scripts 24/24 + 24/24 + 22/22 (2026-03-21)
- ✅ doc/AgentC.md deleted: pre-implementation design doc with inaccurate syntax superseded by README.md + edict_language_reference.md (2026-03-21)
- ✅ G020: Early type binding — `bindTypes()` post-parse pass; `ns` param eliminated from entire boxing API; `BindTypesCreatesTypeDef` test; `test_boxing_ffi.sh` 24/24 (2026-03-21)
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
- G046 — Continuation-Based Speculation — **In Progress**
- G049 — Edict VM Multithreading — **In Progress**
- G050 — Thread Spawn/Join Mixed-Run Stability — **In Progress**
- G047 — Native Relational Syntax — **Complete**
- G048 — Library-Backed Logic Capability — **Complete**

### Completed Goals (recent)
- ✅ G048 — Library-Backed Logic Capability — **COMPLETE** (2026-03-23). Detached kanren from VM ownership: evaluator lives in `kanren/`, runtime/import ABIs landed, Edict-side imported-capability coverage is green, pure Edict wrappers prove canonical spec construction, compiler-native logic lowering and `logic { ... }` compiler sugar were removed, `VMOP_LOGIC_RUN` was retired, and the main README/demo/reference docs now reflect the imported-capability model.
- ✅ G047 — Native Relational Syntax — **COMPLETE** (2026-03-23). The remaining follow-up converged on canonical object/Listree specs plus imported evaluation and ordinary Edict wrappers, with final docs aligned to that post-detachment story.
- ✅ G045 — Language Enhancement Review — **COMPLETE** (2026-03-22). Reviewed docs/code/tests to infer AgentC's intended role as a human+agent language; produced `LocalContext/Knowledge/WorkProducts/AgentCLanguageEnhancements-2026-03-22.md` with 14 forward-looking language/runtime/tooling proposals.
- ✅ G044 — JSON-based Module Import Caching — **COMPLETE** (2026-03-22). File-based JSON caching of resolver output implemented in `EdictVM` to bypass `libclang` overhead. Cache stored in `~/.cache/agentc/` with `mtime` invalidation. Included automated test `demo_import_cache.sh`.
- ✅ G021 — Remove Module Name from Resolver Import API — **COMPLETE** (2026-03-22). `scopeName` removed from `ImportRequest`/`ImportResult` structs, all service methods, wire protocol (bumped to `protocol_v2`), all 4 VM opcodes, 2 thunks, service tests, callback tests, and language reference. 7/7 suites pass; all shell scripts pass.
- ✅ G020 — Early type binding (`bindTypes()`) in `Mapper::materialize()` — `ns` param eliminated from boxing API; `BindTypesCreatesTypeDef` test added; 7/7 suites pass (2026-03-21)
- ✅ Boxing nested struct support (2026-03-21) — `ns` param added to `agentc_box`/`agentc_unbox`; nested struct field lookup via namespace; `demo/demo_complex.h` with 10 scalar types + `InnerPoint` nested struct; `test_boxing_ffi.sh` 24/24; committed
- ✅ G018 Phase D — Remove VM Boxing Opcodes (2026-03-21) — `VMOP_BOX/UNBOX/BOX_FREE` removed; `evalDispatchFFI` path; `ffi_type_ltv_handle` size/encoding bugs fixed; 7/7 pass; `test_boxing_ffi.sh` 11/11; `demo_boxing.sh` clean
- ✅ G019 — SlabId LTV Type Unification (2026-03-21) — `void* LTV` → `SlabId LTV`; `libboxing.so` eliminated; `boxing_export.cpp` in cartographer; `FFI_TYPE_UINT32`; tests updated; 7/7 pass
- 🔗[G018 — FFI LTV Passthrough](./Knowledge/Goals/G018-FfiLtvPassthrough/index.md) — All phases A–D + E complete

### Recommended Next Steps
1. **Continue G049 Follow-Through**: Document the completed G050 stabilization result and keep watching for regressions while advancing the remaining multithreading validation work.
2. **Document G049 Limits**: Record the landed first-slice model explicitly: fresh VM per thread, imported callback entrypoints, protected shared-value cells, raw ABI `ltv` handle discipline in the helper, and no general live-graph sharing.
3. **Trim G049 Helper Surface**: Decide whether the auxiliary status-returning thread-entry path should remain or whether the direct `ltv` path is sufficient for the first slice.
4. **Harden G046**: Decide whether G046 needs repeated planner-style probe coverage beyond the newly landed isolation tests.

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
        - G018 — FFI LTV Passthrough — **Complete (via G019)** 🔗[index](./Knowledge/Goals/G018-FfiLtvPassthrough/index.md)
        - G021 — Remove Module Name from Resolver Import API — **Complete** 🔗[index](./Knowledge/Goals/G021-RemoveModuleName/index.md)
        - G019 — SlabId LTV Type Unification — **Complete** 🔗[index](./Knowledge/Goals/G019-SlabIdLtvUnification/index.md)
        - G045 — Language Enhancement Review — **Complete** 🔗[index](./Knowledge/Goals/G045-LanguageEnhancementReview/index.md)
        - G046 — Continuation-Based Speculation — **In Progress** 🔗[index](./Knowledge/Goals/G046-ContinuationBasedSpeculation/index.md)
        - G047 — Native Relational Syntax — **Complete** 🔗[index](./Knowledge/Goals/G047-NativeRelationalSyntax/index.md)
        - G048 — Library-Backed Logic Capability — **Complete** 🔗[index](./Knowledge/Goals/G048-LibraryBackedLogicCapability/index.md)
        - G049 — Edict VM Multithreading — **In Progress** 🔗[index](./Knowledge/Goals/G049-EdictVMMultithreading/index.md)
        - G050 — Thread Spawn/Join Mixed-Run Stability — **In Progress** 🔗[index](./Knowledge/Goals/G050-ThreadSpawnJoinMixedRunStability/index.md)

### Facts
(none yet)

### Concepts
- 🔗[`UnifiedLiteralModel`](./Knowledge/Concepts/UnifiedLiteralModel/index.md) — Edict intentionally unifies code and data literals; intent is determined by evaluation context rather than separate syntax classes.

### Procedures
(none yet)

### WorkProducts
- WP001 — CodebaseReview: Full issue catalog, architectural analysis, 20 prioritized recommendations (2026-03-16)
- WP002 — EdictQuoteHandling: Investigation of `"` semantics in edict compiler/VM; findings for G014 (2026-03-16)
- AgentCLanguageEnhancements-2026-03-22 — design review and proposal set for human+agent language evolution (2026-03-22)
- ContinuationBasedSpeculationPlan-2026-03-22 — implementation plan plus 2026-03-23 status for in-VM speculative execution via nested code-frame execution and checkpoint rollback, now including rewrite/closure/nested-speculation hardening coverage (2026-03-22)
- NativeRelationalSyntaxPlan-2026-03-22 — implementation plan plus 2026-03-23 completion status for logic ergonomics that ultimately converged on canonical object/Listree specs, imported evaluation, wrapper-based sugar, and grouped conjunctive `all(...)` branches inside canonical `conde` objects (2026-03-22)
- LogicCapabilityMigrationPlan-2026-03-23 — follow-on implementation plan plus completed G048 slices for canonical imported logic capability, extracted evaluator ownership in `kanren/`, the runtime ABI capability boundary via `agentc_runtime_ctx*` / `agentc_value` / `agentc_logic_eval(...)`, direct imported `agentc_logic_eval_ltv(...)`, Edict-side imported-capability coverage through Cartographer resolve/import, pure Edict wrapper construction of canonical logic specs, removal of compiler-native logic lowering and `logic { ... }` compiler sugar, retirement of `VMOP_LOGIC_RUN`, and final doc alignment around the imported-capability model (2026-03-23)
- EdictVMMultithreadingPlan-2026-03-23 — planning artifact plus in-progress status for pthread-backed threaded thunk execution via imported FFI callbacks, callback-root copying/import preloading, mutex-protected shared-value cells, and corrected raw ABI `ltv` handle handling in the helper layer (2026-03-23)
- G050-ThreadSpawnJoinMixedRunStability — focused defect-isolation goal for the remaining standalone-full-suite instability around `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult` (2026-03-23)

---

## Timeline Highlights

### Recent Events (Last 7 Days)
- 🔗[2026-04-03: Session 1500-1630](./Knowledge/Timeline/2026/04/03/1500-1630/index.md) — Pursued G050 by reproducing the thread-runtime flake, fixing helper/test dependency drift plus atomic slab-handle retain, and restoring repeated focused thread-runtime stability
- 🔗[2026-03-23: Session 2045-2100](./Knowledge/Timeline/2026/03/23/2045-2100/index.md) — Captured the remaining mixed-run `ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult` instability into focused goal G050 so the defect can be debugged without losing context
- 🔗[2026-03-23: Session 1835-1850](./Knowledge/Timeline/2026/03/23/1835-1850/index.md) — Stabilized G049 enough that regression coverage, the full callback suite, and `ctest` are green after raw ABI handle fixes and cleaner callback stack setup
- 🔗[2026-03-23: Session 2209-2230](./Knowledge/Timeline/2026/03/23/2209-2230/index.md) — Advanced G049 into active implementation: helper library, callback-root copying/import preloading, and individually passing focused thread/shared-value tests; remaining work is full-suite stabilization
- 🔗[2026-03-23: Session 1805-1825](./Knowledge/Timeline/2026/03/23/1805-1825/index.md) — Planned G049 around pthread-backed threaded thunk execution with fresh VMs per thread and protected shared-value cells as the first concurrency boundary
- 🔗[2026-03-23: Session 1800-1815](./Knowledge/Timeline/2026/03/23/1800-1815/index.md) — Added archival-status framing to older timeline entries so superseded logic migration states read clearly as history, and marked the workspace ready for the next plan/goal
- 🔗[2026-03-23: Session 1700-1715](./Knowledge/Timeline/2026/03/23/1700-1715/index.md) — Completed a broader stale-doc sweep across remaining demo and fact artifacts so they now describe imported object-spec evaluation instead of pre-detachment logic forms
- 🔗[2026-03-23: Session 1750-1805](./Knowledge/Timeline/2026/03/23/1750-1805/index.md) — Annotated older work products/plans with explicit historical-status notes so superseded logic/compiler paths no longer read as current guidance
- 🔗[2026-03-23: Session 1615-1630](./Knowledge/Timeline/2026/03/23/1615-1630/index.md) — Aligned README, demo, and language-reference docs to the post-G048 logic story: imported object-spec evaluation plus ordinary Edict wrapper-based sugar
- 🔗[2026-03-23: Session 1420-1435](./Knowledge/Timeline/2026/03/23/1420-1435/index.md) — Removed the last `logic { ... }` compiler special case, converted remaining non-duplicative tests to imported object-spec flows, and kept focused validation plus `ctest` green
- 🔗[2026-03-23: Session 1410-1430](./Knowledge/Timeline/2026/03/23/1410-1430/index.md) — Completed G048 by removing compiler-native logic lowering from `EdictCompiler`, retiring `VMOP_LOGIC_RUN`, and keeping focused detached-logic tests plus full `ctest` green
- 🔗[2026-03-23: Session 1330-1345](./Knowledge/Timeline/2026/03/23/1330-1345/index.md) — Detached active kanren ownership from the VM by removing builtin logic thunks, routing logic tests through imported `libkanren.so`, and adding direct imported `agentc_logic_eval_ltv(...)`
- 🔗[2026-03-23: Session 1315-1330](./Knowledge/Timeline/2026/03/23/1315-1330/index.md) — Proved that pure Edict wrappers can build canonical logic specs and evaluate them through imported `libkanren.so`, strengthening the path toward a fully optional kanren plugin
- 🔗[2026-03-23: Session 1240-1255](./Knowledge/Timeline/2026/03/23/1240-1255/index.md) — Added Edict-side imported-capability coverage for the G048 runtime ABI via `resolver.import_resolved !` against `libkanren.so`; focused imported logic tests and full `ctest` stayed green
- 🔗[2026-03-23: Session 1230-1245](./Knowledge/Timeline/2026/03/23/1230-1245/index.md) — Added the first G048 runtime ABI boundary in `kanren/` with `agentc_runtime_ctx*`, opaque `agentc_value` handles, and `agentc_logic_eval(...)`; focused logic tests and full `ctest` stayed green
- 🔗[2026-03-23: Session 1110-1125](./Knowledge/Timeline/2026/03/23/1110-1125/index.md) — Moved `logic_evaluator` ownership into `kanren/`, kept `op_LOGIC_RUN()` as a thin adapter, and verified focused logic tests plus full `ctest`
- 🔗[2026-03-23: Session 1055-1110](./Knowledge/Timeline/2026/03/23/1055-1110/index.md) — Extracted reusable `logic_evaluator` helper, reduced `op_LOGIC_RUN()` to an adapter, added direct helper coverage, and kept `86` Edict tests plus `7/7` CTest targets green
- 🔗[2026-03-23: Session 1030-1045](./Knowledge/Timeline/2026/03/23/1030-1045/index.md) — Landed the first G048 slice: object specs, `[...] logic!`, and `logic(...)` now share one evaluator path; `85` Edict tests and `7/7` CTest targets pass
- 🔗[2026-03-23: Session 1010-1030](./Knowledge/Timeline/2026/03/23/1010-1030/index.md) — Hardened G046 with rewrite/closure/nested-speculation isolation tests, extended G047 `conde(...)` lowering to grouped branches, updated language reference, 7/7 targets pass
- 🔗[2026-03-23: Session 1020-1040](./Knowledge/Timeline/2026/03/23/1020-1040/index.md) — Planned G048 as the follow-on path for canonical `[...] logic!` evaluation and library-backed logic capability migration
- 🔗[2026-03-23: Session 0100-0120](./Knowledge/Timeline/2026/03/23/0100-0120/index.md) — Implemented first-pass G046 in-VM speculation and G047 native `logic(...)` lowering; added tests; 78/78 pass
- 🔗[2026-03-23: Session 0035-0045](./Knowledge/Timeline/2026/03/23/0035-0045/index.md) — Refined G047 toward call-form logic, future capability migration, and rewrite-hosted DSLs
- 🔗[2026-03-23: Session 0005-0015](./Knowledge/Timeline/2026/03/23/0005-0015/index.md) — Refined G047 toward `logic(...)` with equivalent `[...] logic!` semantics
- 🔗[2026-03-22: Session 2345-2359](./Knowledge/Timeline/2026/03/22/2345-2359/index.md) — Planned G046 continuation-based speculation and G047 native relational syntax
- 🔗[2026-03-22: Session 2335-2355](./Knowledge/Timeline/2026/03/22/2335-2355/index.md) — Refined G045 to preserve the intentional unified literal model
- 🔗[2026-03-22: Session 2340-2350](./Knowledge/Timeline/2026/03/22/2340-2350/index.md) — Updated remaining G045 proposals to preserve unified literals
- 🔗[2026-03-22: Session 2320-2340](./Knowledge/Timeline/2026/03/22/2320-2340/index.md) — Language enhancement review and design write-up
- 🔗[2026-03-22: Lexer Unicode and Quotes Fix](./Knowledge/Timeline/2026/03/22/index.md) — Fixed lexer to allow double quotes and UTF-8 characters in Edict identifiers.
- 🔗[2026-03-22: Session 2140-2200](./Knowledge/Timeline/2026/03/22/2140-2200/index.md) — HRM Bootstrap and project state verification
- 🔗[2026-03-22: G021 module name removed + edict string literal migration](./Knowledge/Timeline/2026/03/22/index.md) — `scopeName` removed from entire resolver import API (structs, methods, wire protocol → `protocol_v2`, opcodes, thunks, tests); `TOKEN_STRING` removed from `compileTerm()`; `'word`/`[multi word]` syntax enforced; `edict_language_reference.md` fully updated; 7/7 suites pass
- 🔗[2026-03-21: G019 SlabId Unification, G018 Phase D, G020 Early Type Binding, Cartographer CLI pipeline, agentc.sh wrappers](./Knowledge/Timeline/2026/03/21/index.md) — `LTV=SlabId`; VM boxing opcodes removed; `bindTypes()` pass; full 5-stage CLI pipeline; `agentc_test`/`agentc_demo`; 90/90 tests; `test_boxing_ffi.sh` 24/24; `test_cartographer_*.sh` 24/24 + 24/24 + 22/22
- 🔗[2026-03-20: G017 Edict Script Mode + G016 LMDB Optional Build](./Knowledge/Timeline/2026/03/20/index.md) — `runScript(istream&)`, `edict -`/`edict FILE` CLI modes; `AGENTC_WITH_LMDB` CMake option; 74/74 tests pass
- 🔗[2026-03-16: HRM Bootstrap + Codebase Review](./Knowledge/Timeline/2026/03/16/0000-0200/index.md) — Initial HRM structure; full code review; G002–G013 created
- 🔗[2026-03-16: Phase 1 and 2 Complete](./Knowledge/Timeline/2026/03/16/2200-2300/index.md) — Final build fix (`demo_capabilities.cpp`) and project wrap-up; all goals completed or deferred.

### Current Phase
Phase 3: Active — G016, G017, G019, G020, G044, G045, G047, and G048 complete. G046 remains in implementation: speculation runs in-VM via nested code-frame execution and checkpoint rollback with deeper rewrite/closure/nested-probe isolation coverage. Active kanren ownership is fully detached from the VM: evaluator ownership lives in `kanren/`, imported capability boundaries exist via both `agentc_runtime_ctx*` / `agentc_value` / `agentc_logic_eval(...)` and direct `agentc_logic_eval_ltv(...)`, Edict-side imported-capability coverage proves those boundaries work through ordinary Cartographer resolve/import, pure Edict wrappers prove canonical logic specs can be built and evaluated through imported `libkanren.so`, compiler-native native-logic lowering and `logic { ... }` compiler sugar have been removed from `EdictCompiler`, `VMOP_LOGIC_RUN` has been retired entirely, the primary README/demo/reference docs now describe the imported-capability model, and older planning, work-product, and timeline artifacts now explicitly frame superseded logic forms as historical. G049 is now in active implementation: imported pthread-backed thread launch, copied-root worker VMs, protected shared-value cells, and corrected raw ABI `ltv` handle handling are green in regression coverage, the full callback suite, and `ctest`, while G050 now tracks the remaining mixed-run stability work around `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult` in a clean standalone full `./build/edict/edict_tests` run. Cartographer CLI pipeline + `agentc.sh` wrappers complete. LMDB persistence goals (G040–G042) ready to proceed.

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
