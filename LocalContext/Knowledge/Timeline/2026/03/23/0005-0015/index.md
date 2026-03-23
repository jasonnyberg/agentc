# Session 0005-0015

## Summary

Refined G047 native relational syntax planning so the preferred human-facing form is `logic(...)`, with `[...] logic!` documented as the equivalent literal/evaluator model underneath.

## Work Completed

- Updated planned goal 🔗[`G047-NativeRelationalSyntax`](../../../Goals/G047-NativeRelationalSyntax/index.md) to pivot from `logic { ... }`-centered wording toward `logic(...)` plus equivalent `[...] logic!` semantics.
- Updated work product 🔗[`NativeRelationalSyntaxPlan-2026-03-22.md`](../../../WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md) with revised objective, examples, grammar, parser strategy, verification notes, and rationale.
- Updated proposal 5 in 🔗[`AgentCLanguageEnhancements-2026-03-22.md`](../../../WorkProducts/AgentCLanguageEnhancements-2026-03-22.md) so the broader design document reflects the same direction.
- Updated `LocalContext/Dashboard.md` and the 2026-03-22 daily timeline to record the refinement.

## Key Conclusions

- `logic(...)` is the cleanest human-facing surface because it reads naturally and does not imply a separate mini-language.
- `[...] logic!` remains the best semantic anchor because it preserves the literal-first, evaluator-second story already present elsewhere in Edict.
- This refinement strengthens the unified literal model and keeps G047 positioned as a compiler-lowering project rather than a VM redesign.

## Context Forward

- Review of G046/G047 planning artifacts remains the next step before implementation.
- This note was later superseded by a follow-up refinement that shifted G047 from clause-like examples toward call-form logic (`fresh(q)`, relation calls, `conde(...)`, `results(q)`) and recorded future directions for capability migration and rewrite-hosted DSLs.
