# Session 2345-2359

## Summary

Created implementation-planning artifacts for two G045 follow-up tracks: continuation-based speculation and native relational syntax.

## Work Completed

- Created planned goal 🔗[`G046-ContinuationBasedSpeculation`](../../../Goals/G046-ContinuationBasedSpeculation/index.md).
- Created planned goal 🔗[`G047-NativeRelationalSyntax`](../../../Goals/G047-NativeRelationalSyntax/index.md).
- Wrote detailed work product 🔗[`ContinuationBasedSpeculationPlan-2026-03-22.md`](../../../WorkProducts/ContinuationBasedSpeculationPlan-2026-03-22.md) covering rationale, target runtime model, implementation slices, examples, risks, and verification.
- Wrote detailed work product 🔗[`NativeRelationalSyntaxPlan-2026-03-22.md`](../../../WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md) covering grammar recommendations, lowering strategy, examples, risks, and verification.

## Evidence Used

- `edict/edict_vm.cpp` transaction checkpoint and `op_SPECULATE()` implementation notes.
- `edict/edict_compiler.cpp` special handling for `logic { ... }` and `speculate [ ... ]`.
- `edict/edict_vm.cpp` `op_LOGIC_RUN()` object-shape expectations.
- `edict/tests/logic_surface_test.cpp` and transaction/cognitive tests for current behavior.

## Key Conclusions

- Continuation-based speculation is primarily a VM execution-model refactor that preserves surface syntax while making speculation consistent with the reversible runtime story.
- Native relational syntax is primarily a compiler-lowering project that preserves the current miniKanren backend and object IR while making human-authored logic much easier to read.

## Context Forward

- Next step is user review of G046 and G047 planning artifacts before implementation starts.
- If approved, G046 should likely begin with a no-semantic-change execution-frame extraction slice, while G047 should likely begin with conjunction-only lowering (`fresh` / `where` / `results` / `limit`).
