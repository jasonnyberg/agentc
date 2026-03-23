# Unified Literal Model

## Scope

- Scope: LOCAL
- Granularity: Layer 2 concept

## Summary

AgentC/Edict intentionally does not enforce a hard distinction between code literals and data literals. The language keeps syntax small by treating literals as one substrate whose role is determined by evaluation context, sigils, and operators such as `!`, rather than by introducing separate syntactic classes for "quoted code" versus "plain data".

## Bigger Picture

This concept supports AgentC's broader identity as a reversible cognitive substrate. A unified literal model aligns with the project's preference for one runtime representation (`ListreeValue`), one evaluation substrate, and minimal syntax branching.

## Detailed View

- `[]` should be understood as part of the language's intentional literal unification, not as accidental syntactic overloading.
- The cognitive simplifier is that programmers reason about literals first, then about how those literals are used or executed.
- Sigils and evaluation forms such as `'literal` and `f(x)` provide intent cues without requiring a separate code/data ontology.
- The main design risk is not semantic confusion inside the language model itself, but readability and onboarding friction for humans who expect conventional quotation boundaries.
- The right follow-up work is therefore explainability, inspection, strictness/tooling, and better pedagogy, not reintroducing separate syntax classes.

## Implications

- Proposal work should preserve literal unification unless there is a very strong reason to split it.
- Tooling should help reveal evaluation intent in context.
- Documentation should teach literal use in terms of "same substrate, different use".

## Related Items

- 🔗[`LocalContext/Knowledge/Goals/G045-LanguageEnhancementReview/index.md`](../../Goals/G045-LanguageEnhancementReview/index.md)
- 🔗[`LocalContext/Knowledge/WorkProducts/AgentCLanguageEnhancements-2026-03-22.md`](../../WorkProducts/AgentCLanguageEnhancements-2026-03-22.md)
- 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)

## Navigation Guide for Agents

- Load this concept when discussing Edict syntax philosophy or evaluating proposals that touch literals, quotation, or code/data boundaries.
- Pair it with the G045 work product when choosing follow-up language changes.
- If future work changes literal semantics, update this concept and the linked work product together.
