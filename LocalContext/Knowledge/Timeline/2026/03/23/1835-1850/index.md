# Session 1835-1850

## Summary

Stabilized the first G049 implementation slice enough that regression coverage, the full callback suite, and `ctest` are green after raw ABI handle fixes and cleaner callback stack setup.

## Work Completed

- Corrected raw ABI `ltv` handle encode/decode in `cartographer/tests/libagentthreads_poc.cpp` so the helper consistently treats the C ABI handle as packed `(index << 16) | offset` data and only decodes at `ltv_api` boundaries.
- Cleaned up the threaded spawn-result test in `edict/tests/callback_test.cpp` so it no longer leaves an extra literal on the stack before `ffi_closure !`, removing a source of mixed-run pollution.
- Rebuilt both `agentthreads_poc` and `edict_tests` together before validation so the helper library and test binary no longer drift during G049 iteration.

## Validation

- `./edict/edict_tests --gtest_filter='*Regression*'` passed (`2/2`).
- `./edict/edict_tests --gtest_filter='CallbackTest.*'` passed (`20/20`).
- `ctest --output-on-failure` passed (`7/7`).

## Outcome

The pthread-backed helper path is now stable enough for ordinary validation runs: direct `ltv` thread spawn/join works, shared-cell snapshot isolation works, and threaded shared-cell update works in the callback suite, while the broader CTest run is green again. The next follow-up is to document the first-slice threading limits and decide whether the auxiliary status-returning helper path should remain.

## Links

- Goal: 🔗[`G049-EdictVMMultithreading`](../../../Goals/G049-EdictVMMultithreading/index.md)
- Work product: 🔗[`EdictVMMultithreadingPlan-2026-03-23.md`](../../../WorkProducts/EdictVMMultithreadingPlan-2026-03-23.md)
- Dashboard: 🔗[`LocalContext/Dashboard.md`](../../../../Dashboard.md)
