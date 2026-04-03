# Session 2209-2230

## Summary

Advanced G049 from planning into active implementation/debugging, with focused pthread-thread and shared-cell tests now working individually.

## Work Completed

- Added pthread-backed helper APIs in `cartographer/tests/libagentthreads_poc.h` / `.cpp` for thread spawn/join and mutex-protected shared-value cells.
- Hardened callback worker execution so `closure_thunk(...)` runs with a copied captured root and preloads imported libraries referenced from callback-scope metadata before executing worker thunks.
- Normalized the helper toward raw ABI `ltv` handling instead of mixing encoded 32-bit handles with C++ `SlabId` objects.
- Reworked focused callback tests so:
  - direct `ltv` thread spawn/join round-trips a string result,
  - shared-cell snapshot isolation passes,
  - threaded shared-cell update passes when run in isolation.

## Validation

- `CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult` passes when run individually.
- `CallbackTest.ImportResolvedThreadRuntimeSharedCellProvidesSnapshotIsolation` passes when run individually.
- `CallbackTest.ImportResolvedThreadRuntimeUpdatesSharedCellFromThread` passes when run individually.
- Full `edict_tests` / `ctest` are not yet stable because the mixed run still aborts, so the slice is in progress rather than complete.

## Outcome

The first G049 runtime path is now credible in code: imported pthread-backed helper entrypoints, copied-root worker VMs, and protected shared-value cells all work in focused scenarios. The remaining work is stabilization under the full mixed test run and then documenting the final first-slice threading model/limits.

## Links

- Goal: 🔗[`G049-EdictVMMultithreading`](../../../Goals/G049-EdictVMMultithreading/index.md)
- Work product: 🔗[`EdictVMMultithreadingPlan-2026-03-23.md`](../../../WorkProducts/EdictVMMultithreadingPlan-2026-03-23.md)
- Dashboard: 🔗[`LocalContext/Dashboard.md`](../../../../Dashboard.md)
