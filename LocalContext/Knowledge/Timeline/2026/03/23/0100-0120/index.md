# Session 0100-0120

## Historical Status

This session records the first G047 implementation slice from before the later G048 detachment work completed. References here to compiler-native `logic(...)`, `logic { ... }`, or `VMOP_LOGIC_RUN` are historical implementation notes rather than current behavior.

## Summary

Implemented the first code slice for G046 continuation-based speculation and G047 native relational syntax, created implementation checklists in both goal records, updated the work products with landing status, and verified the full Edict test suite passes.

## Work Completed

- Re-reviewed `G046` and `G047` goals/plans against the live codebase and confirmed both had a clear implementation path.
- Updated `LocalContext/Knowledge/Goals/G046-ContinuationBasedSpeculation/index.md` with an implementation checklist, in-progress status, and landed runtime notes.
- Updated `LocalContext/Knowledge/Goals/G047-NativeRelationalSyntax/index.md` with an implementation checklist, in-progress status, and landed compiler/lowering notes.
- Updated `LocalContext/Knowledge/WorkProducts/ContinuationBasedSpeculationPlan-2026-03-22.md` with the code that landed, tests run, and remaining follow-up.
- Updated `LocalContext/Knowledge/WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md` with the code that landed, tests run, and remaining follow-up.

## Code/Behavior Outcome

### G046

- `op_SPECULATE()` no longer clones a second `EdictVM`.
- Execution now uses `runCodeLoop(...)` plus `executeNested(...)` so speculative bytecode can run inside the current VM.
- Transaction checkpoints can preserve `VMRES_CODE` during rollback via `restoreCodeResource=false`, allowing checkpointed nested execution from inside the active loop.

### G047

- `logic(...)` now lowers through `compileNativeLogicCall()` into the existing object-shaped IR used by `VMOP_LOGIC_RUN`.
- The MVP supports `fresh(...)`, supported relation calls, `conde(...)`, `results(...)`, and `limit(...)`.
- Existing `logic { ... }` behavior remains compatible.

## Validation

- Built `edict_tests` successfully.
- Focused coverage passed:
  - `VMStackTest.SpeculateLiteralReturnsResultWithoutChangingBaseline`
  - `VMStackTest.SpeculateFailureReturnsNullWithoutChangingBaseline`
  - `VMStackTest.SpeculateRollsBackNestedMutationInRunningVm`
  - `LogicSurfaceTest.NativeLogicCallSupportsMembershipQueries`
  - `LogicSurfaceTest.NativeLogicCallSupportsCondeAndLimit`
- Full suite passed: `78 tests from 13 test suites`.

## Remaining Follow-Up

- G046: add deeper isolation/hardening coverage and update the language/runtime reference.
- G047: add richer `conde(...)` lowering and/or explicit `[...] logic!` equivalence coverage, then update the language/reference docs.

## Links

- 🔗[`G046-ContinuationBasedSpeculation`](../../../Goals/G046-ContinuationBasedSpeculation/index.md)
- 🔗[`G047-NativeRelationalSyntax`](../../../Goals/G047-NativeRelationalSyntax/index.md)
- 🔗[`ContinuationBasedSpeculationPlan-2026-03-22.md`](../../../WorkProducts/ContinuationBasedSpeculationPlan-2026-03-22.md)
- 🔗[`NativeRelationalSyntaxPlan-2026-03-22.md`](../../../WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md)
- 🔗[`Dashboard.md`](../../../../Dashboard.md)
