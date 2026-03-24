# G047 - Native Relational Syntax

## Status: COMPLETE

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
- [x] Update remaining language/reference material so it no longer presents `logic(...)`, `logic { ... }`, or `VMOP_LOGIC_RUN` as active implementation paths.

## Follow-On

- 🔗[`G048-LibraryBackedLogicCapability`](../G048-LibraryBackedLogicCapability/index.md) is complete and provides the detached imported capability boundary.
- G047 is now complete: the active logic story is canonical object specs, imported evaluation, and optional wrapper-based sugar.

## Implementation Progress (2026-03-23)

- Earlier G047 work proved that canonical logic specs can represent readable relational queries and that grouped conjunctive `conde(...)` branches can be expressed against the same stable object form.
- G048 then completed the architecture detachment: imported capability boundaries now evaluate canonical specs, pure Edict wrappers prove higher-level sugar can be built without compiler-native logic parsing, compiler-native `logic(...)` lowering is gone, `logic { ... }` compiler support is gone, and `VMOP_LOGIC_RUN` is retired.
- README, `demo/demo_cognitive_core.sh`, and `LocalContext/Knowledge/WorkProducts/edict_language_reference.md` now describe the post-detachment model instead of older compiler-native logic forms.
- Validation remains green after the cleanup: focused detached-logic tests passed, the updated cognitive-core demo runs successfully, and full `ctest` remained `7/7`.

## Final Outcome

### Landed Direction

- Canonical object/Listree logic specs remain the machine-facing contract.
- Imported `kanren` capability paths now evaluate those specs through ordinary FFI/import plumbing.
- Higher-level call-shaped sugar can be written as ordinary Edict wrappers rather than compiler or VM special cases.
- Rewrite-hosted DSL sugar remains a valid future ergonomic layer, but it now sits above the imported capability path rather than inside the compiler or VM.

### Explicitly Not Chosen

- Keeping `logic(...)` as a compiler-native surface.
- Keeping `logic { ... }` as a compiler special case.
- Keeping `VMOP_LOGIC_RUN` or any VM-owned miniKanren execution path.

## Navigation Guide for Agents

- Treat G047 as complete.
- The active logic story is: canonical object/Listree spec + imported `kanren` evaluator + optional ordinary Edict wrappers.
- Do not reintroduce compiler-native `logic(...)`, `logic { ... }`, or VM-owned logic execution unless a new goal explicitly reopens that design.
- If pursuing future ergonomics, prefer wrapper libraries or rewrite-hosted sugar over compiler or VM special cases.
