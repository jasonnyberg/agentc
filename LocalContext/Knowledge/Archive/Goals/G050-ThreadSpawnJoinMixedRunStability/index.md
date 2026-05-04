# G050 - Thread Spawn/Join Mixed-Run Stability

## Status: COMPLETE

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Parent runtime goal: 🔗[`G049-EdictVMMultithreading`](../G049-EdictVMMultithreading/index.md)
- Related FFI precedent: 🔗[`G018-FfiLtvPassthrough`](../G018-FfiLtvPassthrough/index.md)

## Goal

Capture and resolve the remaining mixed-run instability around `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult`, so the pthread-backed spawn/join path is reliable not only in focused callback runs but also in a clean standalone full `./build/edict/edict_tests` run.

## Why This Matters

- G049's first slice is now strong enough that regression coverage, the full callback suite, and `ctest` are green, but this one test still remains the best signal that mixed-run state and callback-thread value flow are not yet fully trustworthy.
- The failure is subtle: the same code path can pass in isolation and fail in a broader suite, which is exactly the kind of issue that can conceal lifetime, stack-pollution, or ABI-boundary bugs.
- Saving the current evidence now prevents the investigation from losing hard-won context if the issue needs a separate focused debugging pass.

## Current Working State

- The remaining failure is no longer best explained purely by callback-stack hygiene or raw `ltv` ABI packing.
- Two concrete contributors are now evidenced:
  - helper/test binary drift because `edict_tests` did not depend on `agentthreads_poc`, so rebuilding only the test binary could leave the pthread helper stale
  - non-atomic slab ref acquisition when constructing shared `CPtr` handles from raw `SlabId` values across threads
- Focused repeated validation is green after fixing both issues, and the broader standalone/full-suite verification has now been re-established.

## Failure To Stabilize

- Test under investigation: `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult`
- Primary observed mixed-run failures:
  - C++ exception: `Pointer not found in any slab`
  - wrong joined value such as an 8-byte pointer-like string instead of `threaded-result`
  - earlier historical failures included invalid callback-pointer crashes and `null` joined results before the current focused fixes landed
- Current status:
- targeted reproductions were recovered with suppressed-output repeat loops, including `-11` crashes and null joined values
- `agentthreads_poc` is now wired as a build dependency of `edict_tests`
- repeated focused thread-runtime runs are now green (`10/10` for `CallbackTest.ImportResolvedThreadRuntime*` after the fixes)
- the apparent later `edict -e` / `ctest` stalls were build-state issues during partial rebuilds, not active runtime failures in the current code
- after a full rebuild, standalone `./build/edict/edict_tests` and `ctest --test-dir build --output-on-failure` both complete successfully

## Evidence Snapshot

**Claim**: The mixed-run instability was at least partly caused by cross-thread lifetime acquisition at the slab-handle boundary and by stale pthread-helper rebuilds, not only by callback-stack or ABI-packing issues.

**Evidence**:
- Primary: focused callback coverage is green after the raw ABI handle fix and closure-stack cleanup, including `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult` when run alone.
- Primary: regression coverage and `ctest --output-on-failure` are green, so the project-level baseline is healthy enough that the defect is now narrow and reproducible in a smaller area.
- Primary: earlier instrumentation in `closure_thunk(...)`, `FFI::invoke(...)`, and `libagentthreads_poc.cpp` showed the direct imported `ltv` path is sound in ordinary calls, while callback-thread runs were sensitive to raw-handle packing and closure/test stack pollution.
- Primary: `cartographer/tests/libagentthreads_poc.cpp` now stores thread args/results and shared-cell contents as raw ABI `ltv` handles with explicit encode/decode helpers, which fixed focused callback runs after a previous inverted packing attempt caused visible corruption.
- Primary: `edict/tests/callback_test.cpp` was cleaned so the spawn-result test binds the closure into `@worker` and avoids leaving extra values on the stack before `threadffi.agentc_thread_spawn_ltv !`, which removed one known source of focused-run pollution.
- Primary: `edict/edict_vm.cpp` still copies captured `ROOT` for callback execution and preloads imported libraries from callback scope metadata, so callback-time import availability is no longer the main unknown.
- Primary: repeated suppressed-output runs of `./build/edict/edict_tests --gtest_filter=CallbackTest.ImportResolvedThreadRuntime*` reproduced `-11` crashes even after rebuilding the helper, showing the instability survived beyond simple build drift.
- Primary: `core/alloc.h` previously acquired shared references with separate `valid(...)` and `modrefs(...)` calls in `CPtr(const SlabId&)`, copy assignment, and `ltv_ref(...)`, leaving a race window where another thread could drop the final reference before the retain completed.
- Primary: `Allocator<T>::tryRetain(...)` now performs ref acquisition under a single allocator lock, and the affected call sites now use it.
- Primary: `edict/CMakeLists.txt` now makes `edict_tests` depend on both `agentmath_poc` and `agentthreads_poc`, preventing helper/test drift during normal targeted rebuilds.
- Primary: after those changes, the thread-runtime callback subset passed `10/10` repeated suppressed-output runs.
- Primary: after a full `cmake --build build`, `./build/edict/edict_tests` passes all 86 tests and `ctest --test-dir build --output-on-failure` passes all 7 registered tests.

**Confidence**: 97%

## Known-Good Facts

- `agentc_thread_spawn_ltv(...)` / `agentc_thread_join_ltv(...)` work in focused callback runs.
- `CallbackTest.ImportResolvedThreadRuntimeSharedCellProvidesSnapshotIsolation` is green.
- `CallbackTest.ImportResolvedThreadRuntimeUpdatesSharedCellFromThread` is green in the focused callback suite.
- `./build/edict/edict_tests --gtest_filter='CallbackTest.ImportResolvedThreadRuntime*'` is green in repeated runs (`10/10`) after the current fixes.
- `gdb -batch --args ./build/edict/edict_tests --gtest_filter='CallbackTest.ImportResolvedThreadRuntime*'` now runs those three tests cleanly.
- `./build/edict/edict_tests --gtest_filter='CallbackTest.*-CallbackTest.EdictCliImportResolvedDemoPrintsExpectedResult'` is green.
- Every non-callback suite completes quickly when run as an isolated chunk; the only observed isolated hang in this pass is `CallbackTest.EdictCliImportResolvedDemoPrintsExpectedResult`.
- `./build/edict/edict_tests` now passes in full (`86/86`).
- `ctest --test-dir build --output-on-failure` now passes in full (`7/7`).
- `ListreeTests` now passes again after a full rebuild; the earlier `ctest` stop at test #1 was due to stale build state, not a newly confirmed Listree runtime regression.

## Known-Risky Areas

- Mixed-run test-state pollution inside `edict/tests/callback_test.cpp`, especially any leftover values around closure construction or imported-function aliasing.
- Drift between `agentthreads_poc` and `edict_tests` binaries when only one target is rebuilt (now mitigated by target dependencies).
- Raw ABI `ltv` handle packing at the helper boundary. The correct current helper discipline is:
  - C ABI storage uses packed `ltv` (`uint32_t`-style handle)
  - decode to C++ `LTV` only at `ltv_api` boundaries
  - encode back before storing/returning from the helper
- Non-atomic slab-handle retention when crossing thread boundaries through raw `SlabId` / `ltv` handles.
- Remaining temporary debug/instrumentation state, which may still affect stderr timing or expose hidden ordering assumptions if left ungated.

## Most Relevant Files

- `edict/tests/callback_test.cpp`
- `cartographer/tests/libagentthreads_poc.h`
- `cartographer/tests/libagentthreads_poc.cpp`
- `edict/edict_vm.cpp`
- `cartographer/ffi.cpp`
- `cartographer/ltv_api.h`
- `cartographer/ltv_api.cpp`
- `core/alloc.h`
- `edict/CMakeLists.txt`

## Most Relevant Implementation Details

- `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult` currently uses the direct `ltv` callback-return path, not the auxiliary status-returning helper path.
- The test now builds the closure explicitly with `ffi_closure !`, binds it to `@worker`, and then calls:
  - `worker 'seed threadffi.agentc_thread_spawn_ltv ! @handle pop`
  - `handle threadffi.agentc_thread_join_ltv ! @result pop`
- `closure_thunk(...)` copies captured `ROOT`, constructs a fresh `EdictVM vm(root)`, binds callback args into `ARG0`, `ARG1`, etc., preloads imported libraries from callback scope metadata, executes the thunk, and refs `ltv` returns before libffi conversion.
- `libagentthreads_poc.cpp` now stores `arg`, `result`, and shared-cell values as raw `ltv`, with helper functions:
  - `decode_ltv_handle(ltv value)`
  - `encode_ltv_handle(LTV value)`
  - `snapshot_ltv(ltv value)`
- `Allocator<T>` now has a `tryRetain(SlabId)` fast path for atomic ref acquisition under the allocator mutex, and `CPtr(const SlabId&)`, `CPtr::operator=(const CPtr&)`, and `ltv_ref(...)` now use it instead of a split `valid()+modrefs()` sequence.
- `edict_tests` now explicitly depends on the helper-library build targets it consumes from `build/cartographer/`.

## Most Relevant Historical Findings

- Earlier invalid callback-pointer crashes were reduced by moving away from patched imported callback-signature metadata and toward explicit `ffi_closure !` construction in the Edict test source.
- Earlier `null` / garbage return issues were narrowed by fixing raw ABI `ltv` handle normalization in `libagentthreads_poc.cpp`.
- Callback-time imported-library availability was improved by adding callback-root copying plus `preload_imported_libraries(vm, root)` inside `closure_thunk(...)`.
- Focused tests became reliable only after rebuilding both `agentthreads_poc` and `edict_tests` together, strongly suggesting helper/test binary drift was part of the instability story.

## Recommended Next Investigation Steps

1. If the thread-runtime flake reappears, first confirm build freshness with `cmake --build build` before treating new hangs as runtime regressions.
2. If mixed-run thread failures reappear, inspect any remaining raw-pointer access paths that still rely on `getPtr(...)` after releasing allocator locks.

## Work Product

- Primary implementation plan: 🔗[`EdictVMMultithreadingPlan-2026-03-23.md`](../../WorkProducts/EdictVMMultithreadingPlan-2026-03-23.md)

## Success Criteria

- `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult` remains green under repeated focused mixed-run-style validation and in a clean standalone full `./build/edict/edict_tests` run.
- No pointer-like garbage strings or `Pointer not found in any slab` exceptions appear in the spawn/join path.
- The direct `ltv` callback-return path is trustworthy enough that the helper surface can be simplified confidently.

## Navigation Guide for Agents

- Treat G050 as a focused defect-isolation child of G049, not a separate architecture track.
- Preserve the distinction between project-level green signals (`ctest`, focused callback suite) and the remaining standalone-full-suite uncertainty.
- Rebuild `agentthreads_poc` together with `edict_tests` whenever changing the helper or callback tests.
- Prefer narrow instrumentation over broad logging unless the mixed-run issue becomes unreproducible again.
