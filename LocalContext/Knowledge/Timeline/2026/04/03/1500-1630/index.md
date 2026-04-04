# Session 1500-1630

## Summary

Pursued G050 from fresh HRM bootstrap through repro, root-cause isolation, partial stabilization, and memory updates.

## Work Completed

- Reproduced the mixed-run instability around `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult` with suppressed-output repeat loops and confirmed the failure could manifest as both null/empty joined values and `-11` crashes.
- Verified that rebuilding `agentthreads_poc` together with `edict_tests` still mattered, then fixed the build graph so `edict_tests` now depends on `agentmath_poc` and `agentthreads_poc` in `edict/CMakeLists.txt`.
- Inspected cross-thread `SlabId` / `ltv` lifetime acquisition and found that `CPtr(const SlabId&)`, `CPtr::operator=(const CPtr&)`, and `ltv_ref(...)` used split `valid()+modrefs()` sequences, leaving a race window during retain.
- Added `Allocator<T>::tryRetain(...)` in `core/alloc.h` and switched those retain sites to use it, so ref acquisition now happens under one allocator lock.
- Re-ran focused validation: `./build/edict/edict_tests --gtest_filter='CallbackTest.ImportResolvedThreadRuntime*'` passed under `gdb` and in `10/10` repeated suppressed-output runs after the fixes.

## Verification

- `cmake -S . -B build && cmake --build build --target edict_tests`
- `cmake --build build --target agentthreads_poc edict_tests`
- `gdb -batch -ex run -ex bt --args ./build/edict/edict_tests --gtest_filter='CallbackTest.ImportResolvedThreadRuntime*'`
- repeated `./build/edict/edict_tests --gtest_filter='CallbackTest.ImportResolvedThreadRuntime*'` (`10/10` green, suppressed output)
- Attempted broader validation, but standalone full `./build/edict/edict_tests` and `ctest --test-dir build --output-on-failure` did not complete inside the available timeout because the current full run remains long-running/noisy.

## Outcome

G050 moved from open-ended repro into a concrete partial fix state: the thread-runtime flake is no longer reproducible in repeated focused runs after landing both the helper dependency fix and atomic slab-handle retain fix. Broader end-to-end completion-level validation still remains before the goal can be marked done.

## Links

- Goal: 🔗[`G050-ThreadSpawnJoinMixedRunStability`](../../../../Goals/G050-ThreadSpawnJoinMixedRunStability/index.md)
- Parent goal: 🔗[`G049-EdictVMMultithreading`](../../../../Goals/G049-EdictVMMultithreading/index.md)
- Dashboard: 🔗[`LocalContext/Dashboard.md`](../../../../../Dashboard.md)
