# G047 - Native Relational Syntax

## Status: PLANNED

## Parent Context

- Parent proposal source: 🔗[`AgentCLanguageEnhancements-2026-03-22.md`](../../WorkProducts/AgentCLanguageEnhancements-2026-03-22.md)
- Related concept: 🔗[`UnifiedLiteralModel`](../../Concepts/UnifiedLiteralModel/index.md)
- Related language review goal: 🔗[`G045-LanguageEnhancementReview`](../G045-LanguageEnhancementReview/index.md)

## Goal

Add a human-oriented native relational syntax centered on call-form `logic(...)` and equivalent literal-plus-operator forms such as `[...] logic!`, lowering to the existing object-based logic IR consumed by `VMOP_LOGIC_RUN` while preserving the unified literal model and opening a path to migrate miniKanren behavior out of VM-owned primitives and into library/FFI-backed capability layers.

## Why This Matters

- Today the available relational surface still pushes humans toward JSON-shaped `logic { ... }` bodies even though the language philosophy points toward ordinary literals plus evaluation.
- The current logic engine is already valuable; the main missing piece is a friendlier notation that still fits the unified literal model and does not create a separate language.
- `logic(...)` is attractive because it reads cleanly while naturally corresponding to a bracket literal consumed by `logic!`, keeping the mental model "literal first, evaluator second" without requiring a VM redesign.
- Call-form goals such as `fresh(q)`, `results(q)`, and where-less `conde(==(q 'tea) ==(q 'coffee))` point toward logic as an ordinary capability over the substrate rather than a special VM-owned domain.
- Existing rewrite facilities suggest a later path for small DSL layers that normalize friendlier surface forms into the native call-form substrate once that substrate is stable.
- This feature improves readability and onboarding while preserving the system's one-substrate philosophy and stable machine-oriented IR.

## Evidence Snapshot

**Claim**: The current logic surface is operationally sound but still IR-shaped.
**Evidence**:
- Primary: `edict/edict_compiler.cpp:404` shows `logic {` currently just compiles the inner body as a JSON object and emits `VMOP_LOGIC_RUN`.
- Primary: `edict/edict_vm.cpp:2981` shows `op_LOGIC_RUN()` expects an object with `fresh`, `where`, `conde`, `results`, and optional `limit`.
- Primary: `edict/tests/logic_surface_test.cpp:132` shows the so-called native block still contains JSON field strings and arrays.
**Confidence**: 98%

## Expected System Effect

- Human-authored relational code becomes shorter and easier to scan.
- Agents keep a stable object IR target when precision matters.
- The compiler gains a lowering path from friendlier source forms into the same existing query object, which keeps VM/runtime changes small in the MVP.
- The language gets a cleaner canonical story: `logic(...)` is presentation sugar over a literal shape that `logic!` can consume.
- Longer term, the system can shrink VM ownership of logic behavior by moving more miniKanren semantics into library or FFI-backed evaluators over ordinary literals.

## Work Product

- Detailed plan: 🔗[`NativeRelationalSyntaxPlan-2026-03-22.md`](../../WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md)

## Scope

### In Scope

- Define an MVP native syntax for call-form `fresh(...)`, relation goals such as `==(...)`, where-less `conde(...)`, `results(...)`, and `limit(...)`, centered on `logic(...)` with equivalent bracket-literal semantics.
- Lower that syntax into the existing logic object IR.
- Preserve current JSON/object `logic_run !` and `logic { ... }` forms for compatibility.
- Add tests and documentation showing semantic equivalence between native syntax and existing IR.
- Document a future path for moving logic execution semantics from VM primitives toward library/FFI-backed evaluators.
- Document rewrite-based DSL sugar as a later layer over the native call-form substrate, not as the MVP implementation mechanism.

### Out of Scope

- Replacing the existing object IR.
- Replacing the existing miniKanren backend in the MVP.
- Broad syntax redesign outside the logic call/literal forms.
- Implementing rewrite-hosted DSL sugar before the native call-form substrate is proven.

## Proposed Phases

1. Define the MVP grammar and exact lowering rules for call-form `logic(...)` and equivalent `[...] logic!` representation.
2. Implement compiler lowering from native syntax to the current query object.
3. Add multi-result, `limit(...)`, and where-less `conde(...)` support.
4. Design a follow-on slice that migrates logic execution semantics toward library/FFI-backed evaluators where practical.
5. Evaluate rewrite-driven DSL sugar as a later normalization layer over the native call-form substrate.
6. Update language docs and examples to teach `logic(...)`, the equivalent literal-plus-`logic!` model, the canonical IR, and the longer-term capability direction.

## Success Criteria

- A simple native `logic(...)` program compiles into the same VM behavior as the current object form.
- The unified literal model remains intact; no alternate quotation domain is introduced.
- `logic(...)` and `[...] logic!` can be explained as equivalent views of the same semantics.
- Representative queries read cleanly in call form, including where-less `conde(...)` cases.
- Existing logic tests continue to pass, with new native-syntax equivalence coverage added.
- The resulting syntax clearly improves readability for representative queries.
- The plan preserves a credible path to move more logic behavior out of the VM core after the syntax substrate is established.

## Acceptance Signals For Implementation Kickoff

- The plan defines a small first grammar rather than an all-at-once parser expansion.
- Lowering targets the current `fresh`/`where`/`results`/`limit` object structure used by `op_LOGIC_RUN()`.
- Examples show immediate readability wins while preserving current semantics and the literal-first evaluation story.
- The follow-on architecture keeps rewrite-based DSLs and library/FFI migration as explicit later stages, not hidden scope creep in the MVP.

## Navigation Guide for Agents

- Load the linked work product before implementation; it contains grammar recommendations, lowering strategy, examples, and risk analysis.
- Keep JSON/object logic as the canonical backend shape until the native syntax slice is proven stable.
- Treat `logic(...)` as the preferred human-facing surface, with `[...] logic!` as the equivalent literal/evaluator interpretation underneath.
- Prefer call-form goals such as `fresh(q)`, `results(q)`, and `conde(==(q 'tea) ==(q 'coffee))` over clause-like forms when refining the syntax direction.
- Treat rewrite-based DSL sugar as a later simplification layer over native Edict forms, not a substitute for getting the base logic substrate right.
- Evaluate all syntax decisions against the unified literal model rather than introducing a separate sublanguage.
