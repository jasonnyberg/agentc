# Session 1020-1040

## Historical Status

This planning note captures an intermediate migration design for G048. Mentions of `logic(...)`, `[...] logic!`, builtin `logic` / `logic_run`, or `VMOP_LOGIC_RUN` are preserved as staged migration history; the final landed model is imported capability evaluation over canonical object/Listree specs plus optional ordinary Edict wrappers.

## Summary

Created a new follow-on planning slice, G048, to turn the current G047 logic-syntax work into a canonical literal-plus-evaluator model centered on real `[...] logic!` evaluation and a later library/FFI-backed logic capability boundary.

## Work Completed

- Reviewed current G047 implementation and confirmed the remaining architectural gap: `logic(...)` currently lowers through compiler-owned structs and still emits `VMOP_LOGIC_RUN` directly.
- Created goal 🔗[`G048-LibraryBackedLogicCapability`](../../../Goals/G048-LibraryBackedLogicCapability/index.md).
- Created work product 🔗[`LogicCapabilityMigrationPlan-2026-03-23.md`](../../../WorkProducts/LogicCapabilityMigrationPlan-2026-03-23.md).
- Updated 🔗[`G047-NativeRelationalSyntax`](../../../Goals/G047-NativeRelationalSyntax/index.md) to link the new follow-on architecture slice.
- Updated `Dashboard.md` and this daily timeline to reflect the new planning direction.

## Key Conclusions

- The current `logic(...)` implementation is useful and stable, but the semantic path is still compiler/VM-owned rather than capability-shaped.
- The safest migration path is not “every miniKanren relation becomes raw FFI immediately.”
- The recommended order is:
  1. make `[...] logic!` real and canonical,
  2. lower `logic(...)` through that path,
  3. thin `VMOP_LOGIC_RUN` into an adapter,
  4. move evaluator ownership behind a reusable library/FFI-backed capability boundary.

## Forward Context

- The next logic-focused implementation slice should likely start with direct `[...] logic!` equivalence coverage and `logic(...) -> logic!` lowering.
- G046 optional follow-up remains planner-style repeated-probe coverage if later planner work depends on it.
