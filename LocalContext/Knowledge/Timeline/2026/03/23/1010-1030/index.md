# Session 1010-1030

## Summary

Completed the requested follow-up slice after the first G046/G047 landing: hardened speculation isolation coverage, extended native logic lowering to grouped conjunctive `conde(...)` branches, updated the language reference to document both landed features, and re-verified the full CTest suite.

## Work Completed

- Added new G046 hardening tests in `edict/tests/vm_stack_tests.cpp` for speculative rewrite-rule rollback, closure-driven mutation rollback, and nested speculation isolation.
- Extended `edict/edict_compiler.cpp` so `logic(...)` can lower grouped conjunctive `conde(...)` branches via `all(...)`, still targeting the same object-shaped query IR consumed by `VMOP_LOGIC_RUN`.
- Added new G047 coverage in `edict/tests/logic_surface_test.cpp` for grouped-branch `conde(...)` behavior.
- Updated `LocalContext/Knowledge/WorkProducts/edict_language_reference.md` to document the landed in-VM speculation model and the call-form native logic syntax, including grouped `conde(...)` branches.
- Updated goal records, work products, Dashboard, and this timeline index so the new validation/doc state is preserved for the next agent.

## Code/Behavior Outcome

### G046

- Speculation hardening now covers rollback of rewrite-rule registration created inside a probe.
- Speculation hardening now covers thunk/closure-driven mutation that targets an outer object through a speculative call path.
- Nested speculation is now explicitly tested so an inner probe can roll back before the outer probe continues.

### G047

- `conde(...)` no longer has to be limited to one goal per branch in the native syntax.
- Grouped conjunctive branches can now be written with `all(...)` inside `conde(...)`.
- The lowering still emits the same `fresh` / `where` / `conde` / `results` / `limit` object IR that `VMOP_LOGIC_RUN` already understands.

## Validation

- Focused tests passed:
  - `VMStackTest.SpeculateRollsBackRewriteRulesDefinedInsideProbe`
  - `VMStackTest.SpeculateRollsBackClosureDrivenMutation`
  - `VMStackTest.NestedSpeculateKeepsInnerMutationIsolatedFromOuterProbe`
  - `LogicSurfaceTest.NativeLogicCallSupportsCondeBranchesWithGroupedGoals`
- Full validation passed: `ctest --output-on-failure` in `build` was `7/7` green.

## Remaining Follow-Up

- G047 still lacks explicit `[...] logic!` equivalence coverage because that form remains conceptual rather than surfaced directly.
- G046 may still want planner-style repeated-probe coverage if later work starts building search/planner constructs directly on nested speculation.

## Links

- 🔗[`G046-ContinuationBasedSpeculation`](../../../Goals/G046-ContinuationBasedSpeculation/index.md)
- 🔗[`G047-NativeRelationalSyntax`](../../../Goals/G047-NativeRelationalSyntax/index.md)
- 🔗[`ContinuationBasedSpeculationPlan-2026-03-22.md`](../../../WorkProducts/ContinuationBasedSpeculationPlan-2026-03-22.md)
- 🔗[`NativeRelationalSyntaxPlan-2026-03-22.md`](../../../WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md)
- 🔗[`edict_language_reference.md`](../../../WorkProducts/edict_language_reference.md)
- 🔗[`Dashboard.md`](../../../../Dashboard.md)
