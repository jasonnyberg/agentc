# G047 - Native Relational Syntax

## Status: IN PROGRESS

## Parent Context

- Parent proposal source: 🔗[`AgentCLanguageEnhancements-2026-03-22.md`](../../WorkProducts/AgentCLanguageEnhancements-2026-03-22.md)
- Related concept: 🔗[`UnifiedLiteralModel`](../../Concepts/UnifiedLiteralModel/index.md)
- Related language review goal: 🔗[`G045-LanguageEnhancementReview`](../G045-LanguageEnhancementReview/index.md)

## Goal

Add a human-oriented relational authoring story that preserves the unified literal model, keeps canonical logic specs as ordinary object/Listree data, and makes higher-level logic sugar expressible as ordinary Edict wrappers over imported logic capability paths rather than VM-owned primitives.

## Why This Matters

- Today the available relational surface is still more IR-shaped than desired even though the language philosophy points toward ordinary literals plus evaluation.
- The current logic engine is already valuable; the main missing piece is a friendlier notation that still fits the unified literal model and does not create a separate language.
- Earlier work used `logic(...)` as an exploration vehicle because it read cleanly and pointed toward a literal-first evaluator story, but the longer-term direction is to keep that sugar in ordinary Edict wrappers rather than compiler-native parsing.
- Call-form goals such as `fresh(q)`, `results(q)`, and where-less `conde(==(q 'tea) ==(q 'coffee))` point toward logic as an ordinary capability over the substrate rather than a special VM-owned domain.
- Existing rewrite facilities suggest a later path for small DSL layers that normalize friendlier surface forms into the native call-form substrate once that substrate is stable.
- This feature improves readability and onboarding while preserving the system's one-substrate philosophy and stable machine-oriented IR.

## Evidence Snapshot

**Claim**: The current logic surface is operationally sound but still more IR-shaped than the desired pure-wrapper end state.
**Evidence**:
- Primary: `edict/tests/callback_test.cpp` now proves ordinary Edict wrapper thunks can build canonical logic specs and evaluate them through imported `libkanren.so` without VM-owned logic syntax.
- Primary: `edict/tests/logic_surface_test.cpp` now exercises object-spec logic through imported evaluator aliases rather than builtin VM logic paths.
- Primary: `kanren/runtime_ffi.h` and `cartographer/tests/kanren_runtime_ffi_poc.h` show the imported capability boundary now exists as a normal FFI surface.
**Confidence**: 98%

## Expected System Effect

- Human-authored relational code becomes shorter and easier to scan.
- Agents keep a stable object IR target when precision matters.
- Higher-level syntax can now live in ordinary Edict wrapper code while preserving the same canonical query object.
- The language gets a cleaner canonical story: logic is data plus imported evaluation, with any sugar implemented above that substrate.
- Longer term, the system can shrink VM ownership of logic behavior by moving more miniKanren semantics into library or FFI-backed evaluators over ordinary literals.

## Work Product

- Detailed plan: 🔗[`NativeRelationalSyntaxPlan-2026-03-22.md`](../../WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md)
- Follow-on architecture goal: 🔗[`G048-LibraryBackedLogicCapability`](../G048-LibraryBackedLogicCapability/index.md)

## Implementation Checklist

- [x] Confirm a stable canonical object/Listree logic spec exists and remains import-friendly.
- [x] Prove ordinary Edict wrappers can build canonical logic specs and evaluate them through imported kanren capability.
- [x] Land imported capability boundaries in `kanren/` and Cartographer coverage proving canonical object-spec evaluation works through FFI.
- [x] Remove compiler-native `logic(...)` lowering and retire `VMOP_LOGIC_RUN` so logic execution is no longer VM-owned.
- [x] Remove compiler support for `logic { ... }` and convert non-duplicative tests to imported object-spec paths.
- [ ] Update remaining language/reference material so it no longer presents `logic(...)`, `logic { ... }`, or `VMOP_LOGIC_RUN` as active implementation paths.

## Follow-On

- 🔗[`G048-LibraryBackedLogicCapability`](../G048-LibraryBackedLogicCapability/index.md) is now complete and provides the detached imported capability boundary.
- Remaining G047 follow-up is documentation/ergonomic alignment around the post-detachment story: canonical object specs, imported evaluation, and wrapper-based sugar.

## Implementation Progress (2026-03-23)

- Earlier G047 work proved that canonical logic specs can represent readable relational queries and that grouped conjunctive `conde(...)` branches can be expressed against the same stable object form.
- G048 then completed the architecture detachment: imported capability boundaries now evaluate canonical specs, pure Edict wrappers prove higher-level sugar can be built without compiler-native logic parsing, compiler-native `logic(...)` lowering is gone, `logic { ... }` compiler support is gone, and `VMOP_LOGIC_RUN` is retired.
- Validation remains green after the cleanup: focused detached-logic tests passed and full `ctest` remained `7/7`.

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
