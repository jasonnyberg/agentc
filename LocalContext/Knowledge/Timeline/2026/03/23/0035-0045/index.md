# Session 0035-0045

## Summary

Refined G047 again after evaluating call-form logic syntax, the possibility of moving more miniKanren behavior out of the VM core, and the possible future use of rewrite rules as a DSL-sugar layer over native Edict forms.

## Work Completed

- Updated planned goal 🔗[`G047-NativeRelationalSyntax`](../../../Goals/G047-NativeRelationalSyntax/index.md) to prefer call-form goals such as `fresh(q)`, `results(q)`, and where-less `conde(==(q 'tea) ==(q 'coffee))`.
- Updated work product 🔗[`NativeRelationalSyntaxPlan-2026-03-22.md`](../../../WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md) with call-form examples, revised phases, a future library/FFI migration direction, and a rewrite-hosted DSL follow-on phase.
- Updated proposal 5 in 🔗[`AgentCLanguageEnhancements-2026-03-22.md`](../../../WorkProducts/AgentCLanguageEnhancements-2026-03-22.md) to reflect the same syntax and architecture direction.
- Updated `LocalContext/Dashboard.md` to reflect the refined G047 framing.

## Evidence Used

- `LocalContext/Knowledge/WorkProducts/edict_language_reference.md` documents rewrite rules as a term-rewriting normalization mechanism with auto/manual/off modes and trace support.
- `edict/edict_vm.cpp` shows rewrite rules are transaction-aware VM state and that rewrite application already exists as a normalizing subsystem.
- Existing G047 planning artifacts already established `logic(...)` / `[...] logic!` alignment with the unified literal model.

## Key Conclusions

- Call-form logic fits Edict better than clause-like forms once the language leans into `f(x)` as the dominant readable pattern.
- Where-less `conde(...)` is cleaner than wrapping each branch in `where(...)` once goals themselves are already expressed as calls.
- The MVP should still be a compiler-lowering project to the current object IR.
- After the syntax substrate is proven, it becomes more plausible to move miniKanren semantics toward library or FFI-backed capability layers.
- Rewrite rules look promising as a later way to host tiny DSLs that normalize into native logic forms, but they should not replace the base substrate design or become hidden MVP scope.

## Context Forward

- User review of the refined G046/G047 planning artifacts remains the next step before implementation.
- If G047 is approved, the first implementation slice should target call-form lowering and equivalence coverage, with capability migration and rewrite-hosted DSL work deferred to later phases.
