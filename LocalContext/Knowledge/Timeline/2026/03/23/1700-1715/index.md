# Session 1700-1715

## Summary

Completed a broader stale-doc sweep after the primary G047/G048 doc alignment, updating remaining non-historical facts and demo sources that still described pre-detachment logic forms.

## Work Completed

- Updated `demo/demo_kanren_edict.cpp` so it now resolves/imports `libkanren.so` through Cartographer and evaluates a canonical object/Listree query spec through imported `logic!` rather than `logic { ... }`.
- Updated `demo/demo_cognitive_core_validation.cpp` so its transaction-time logic example uses the imported `logic` alias instead of `logic_run !` wording.
- Updated `LocalContext/Knowledge/Facts/J3_AgentC/K028_Edict_MiniKanren_Surface.md` to describe the post-detachment model:
  - canonical object/Listree specs,
  - imported evaluator paths,
  - wrapper-based sugar,
  - refreshed examples and references.
- Updated `LocalContext/Knowledge/Facts/J3_AgentC/K025_J3_Logic.md` so its Edict integration notes now point to imported capability evaluation and current demos.

## Validation

- Built `demo_kanren_edict` and `demo_cognitive_core_validation` successfully with CMake after the updates.
- Verified no remaining `logic { ... }`, `logic(...)`, `VMOP_LOGIC_RUN`, or `logic_run !` references remain in `demo/*.cpp`.
- Verified no stale `logic { ... }` / `logic_run !` references remain in `LocalContext/Knowledge/Facts/*.md`.

## Outcome

- The remaining non-historical demo/fact artifacts now match the imported-capability logic architecture.
- What still remains is mostly historical/planning material, where older terminology is preserved as session history rather than active guidance.

## Remaining Follow-Up

- Optional archival cleanup only: older planning and timeline artifacts still mention pre-detachment logic forms as part of the historical record.

## Links

- Goal: 🔗[`G047-NativeRelationalSyntax`](../../../Goals/G047-NativeRelationalSyntax/index.md)
- Goal: 🔗[`G048-LibraryBackedLogicCapability`](../../../Goals/G048-LibraryBackedLogicCapability/index.md)
- Dashboard: 🔗[`Dashboard`](../../../../Dashboard.md)
