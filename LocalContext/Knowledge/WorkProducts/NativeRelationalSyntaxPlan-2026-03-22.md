# Native Relational Syntax Plan

## Objective

Add a readable native syntax centered on call-form `logic(...)` that lowers into the existing object-shaped logic IR used by `VMOP_LOGIC_RUN`. The MVP backend stays the same, but the syntax should also establish a path toward moving miniKanren behavior out of VM-owned primitives and into library/FFI-backed evaluators whose semantics still match a literal consumed by `logic!`.

## Current Baseline

### What exists today

- `logic { ... }` already exists as a surface construct.
- The compiler currently treats the block body as a JSON-like object.
- `op_LOGIC_RUN()` expects the object fields `fresh`, `where`, `conde`, `results`, and optional `limit`.
- The language already has a natural literal-first pattern, so `logic(...)` can be explained as cleaner call syntax for a relational literal that could also appear as `[...] logic!`.
- The VM already has rewrite facilities that normalize stack terms after execution, which suggests a possible future layer for DSL-style sugar once a stable native logic substrate exists.

### What is missing

- Humans still write field names and nested arrays directly.
- The current block reads like serialized IR, not like a native relational language.
- There is no lowering layer that translates a friendlier clause form into the stable backend object.
- Logic execution is still conceptually tied to VM-owned primitives rather than looking like an ordinary capability implemented over the substrate.

## Why Change It

### Human effect

- Queries become easier to read aloud and reason about.
- Common cases no longer require mentally decoding nested array structure.

### System effect

- The VM stays unchanged or nearly unchanged because the compiler lowers to the same IR.
- Agents can continue emitting the stable object form when exactness matters.
- Documentation can teach one concept in three aligned representations: `logic(...)`, `[...] logic!`, and canonical IR.
- The call-form surface creates a cleaner migration path if miniKanren operations later move into FFI/library layers.
- Rewrite rules can later provide even lighter domain sugar without needing to become the foundational representation.

## Design Constraints

- Preserve the current object IR as the canonical backend form.
- Preserve the unified literal model; do not invent a separate quotation or term language.
- Prefer syntax that reads like ordinary Edict evaluation rather than a dedicated mini-language.
- Keep the first grammar small enough to implement and test confidently.
- Preserve existing JSON/object forms for backward compatibility and tooling.
- Treat rewrite-driven DSLs as a later layer; do not make runtime rewrite machinery the MVP parser for native logic.

## Recommended MVP Syntax

The first slice should target call-form queries. The preferred human-facing spelling is `logic(...)`, and it should be documented as equivalent in intent to a bracket literal consumed by `logic!`.

```edict
logic(
  fresh(q)
  membero(q [tea cake jam])
  results(q)
)
```

Equivalent model:

```edict
[
  fresh(q)
  membero(q [tea cake jam])
  results(q)
] logic!
```

Suggested lowerings:

- `fresh(q)` -> `{"fresh": ["q"]}`
- `membero(q [tea cake jam])` -> append ` ["membero", "q", ["tea", "cake", "jam"]] ` to `where`
- `results(q)` -> `{"results": ["q"]}`

## Recommended MVP Grammar

### Core forms

- `fresh(<name>...)`
- `<relation>(<term>...)`
- `conde(<goal>...)`
- `results(<term>...)`
- `limit(<literal>)`

### Surface forms

- Preferred surface: `logic( ... )`
- Equivalent literal/evaluator form: `[ ... ] logic!`
- Compatibility form: existing `logic { ... }` object-style body

### Initial relation coverage

The first slice should lower only relations already handled by `buildLogicGoal()`:

- `==`
- `membero`
- `conso`
- `pairo`
- `caro`
- `cdro`

Anything outside that set should remain a compile error in the native syntax path until explicitly added.

### Preferred disjunction form

Inside `conde(...)`, explicit `where(...)` wrappers should be unnecessary once goals are already in call form. Preferred direction:

```edict
logic(
  fresh(q)
  conde(
    ==(q 'tea)
    ==(q 'coffee)
  )
  results(q)
)
```

This keeps disjunction visually direct and avoids wrapping goals in an extra query-only noun once the call syntax is already established.

## Examples And System Effects

### Example 1: basic membership query

Source:

```edict
logic(
  fresh(q)
  membero(q [tea cake jam])
  results(q)
)
```

Lowered IR shape:

```json
{
  "fresh": ["q"],
  "where": [["membero", "q", ["tea", "cake", "jam"]]],
  "results": ["q"]
}
```

System effect:

- No new logic engine behavior.
- Better readability for humans.
- Same backend representation for tests, tooling, and agent emission.
- Clearer alignment with the unified literal model because `logic(...)` can be taught as clean sugar over `[...] logic!`.

### Example 2: where-less `conde`

```edict
logic(
  fresh(q)
  conde(
    ==(q 'tea)
    ==(q 'coffee)
  )
  results(q)
)
```

Lowered IR shape:

```json
{
  "fresh": ["q"],
  "conde": [
    [["==", "q", "tea"]],
    [["==", "q", "coffee"]]
  ],
  "results": ["q"]
}
```

System effect:

- Disjunction reads as direct composition of goals instead of as nested clause containers.
- The syntax feels more like ordinary Edict function application and less like a separate DSL.
- If logic later migrates toward library/FFI execution, forms like `conde(...)` and `==(...)` are easier to imagine as ordinary capabilities over substrate values.

### Example 3: multi-result query

```edict
logic(
  fresh(head tail)
  conso(head tail [tea cake])
  results(head tail)
)
```

which lowers to:

```json
{
  "fresh": ["head", "tail"],
  "where": [["conso", "head", "tail", ["tea", "cake"]]],
  "results": ["head", "tail"]
}
```

System effect:

- Human authors stop spelling raw JSON arrays.
- `op_LOGIC_RUN()` still consumes the same object layout.
- No additional VM change is needed to justify `logic(...)`; it remains a compiler-facing design choice.

### Example 4: explicit result limit

```edict
logic(
  fresh(q)
  membero(q [tea cake jam])
  results(q)
  limit('2)
)
```

System effect:

- Native syntax continues to use ordinary Edict literals.
- The compiler lowers `limit` to the same string-valued field the VM already parses.
- The same query could also be represented as a literal and passed to `logic!`, preserving inspectability and storage.

### Example 5: rewrite-hosted DSL as a later layer

Possible future direction once call-form logic is stable:

```edict
'manual rewrite_mode ! /
{ "pattern": ["tea_or_coffee", "$q"],
  "replacement": ["conde", ["==", "$q", "tea"], ["==", "$q", "coffee"]] } rewrite_define ! /

logic(
  fresh(q)
  tea_or_coffee(q)
  results(q)
)
```

Conceptual system effect:

- Domain-specific sugar can live above native Edict forms rather than demanding new parser or VM constructs for each niche.
- Rewrite rules remain a normalization/authoring aid, not the semantic foundation.
- The core language stays centered on a small native substrate while still allowing higher-level vocabularies to emerge.

## Recommended Grammar Strategy

### Phase 1 - Native call-form core

Support only:

- one or more `fresh(...)` forms,
- supported relation calls such as `==(...)`, `membero(...)`, `conso(...)`,
- one `results(...)` form,
- optional `limit(...)`.

This slice already covers most of the examples in the current tests.

### Phase 2 - Where-less `conde(...)`

Once Phase 1 is stable, add grouped disjunction as direct goal composition:

```edict
logic(
  fresh(q)
  conde(
    ==(q 'tea)
    ==(q 'coffee)
  )
  results(q)
)
```

This keeps disjunction visibly grouped and still lowers to the current `conde` array-of-arrays representation.

### Phase 3 - Capability migration design

Design how `logic!`, `fresh`, `conde`, and relation operators could move from VM-owned behavior toward library or FFI-backed execution over ordinary substrate values. This phase should focus on architecture and seams, not immediate backend replacement.

### Phase 4 - Inspection-friendly lowering

Expose or document a compiler/debug mode that shows the lowered object IR for a native logic form. This would pair well with the literal-intent inspection idea from G045 and help explain the relationship among `logic(...)`, `[...] logic!`, and object IR.

### Phase 5 - Rewrite-hosted DSL experiments

Once the native call-form substrate is stable, evaluate whether rewrite rules can provide tiny domain-specific sugar layers that normalize into `logic(...)` and related native forms without expanding the parser or VM surface.

## Parser / Compiler Impact

The compiler already recognizes `logic { ... }` as a special form and already has call-style syntax elsewhere. The native syntax slice should:

1. accept `logic(...)` as the preferred clause-oriented surface,
2. preserve current `logic { ... }` object-style behavior,
3. parse call-form goals into an internal temporary representation,
4. lower that representation into the same query object currently produced by JSON compilation,
5. keep the conceptual equivalence to `[...] logic!` explicit in docs and debugging views,
6. emit `VMOP_LOGIC_RUN` as today.

This keeps the VM largely unchanged.

## Risks And Mitigations

### Risk 1: Parser ambiguity

- Native logic syntax can become ambiguous if both clause-like and call-like forms are accepted too broadly at once.
- Mitigation: choose call-form as the preferred native style and reject mixed alternatives in the MVP.

### Risk 2: Accidental sublanguage growth

- Logic syntax could drift into a separate little language.
- Mitigation: preserve ordinary Edict literals, teach `logic(...)` as presentation sugar over literal-plus-evaluator semantics, and continue treating the object form as canonical.

### Risk 3: Premature VM/FFI migration

- Trying to move miniKanren semantics out of the VM before the native syntax substrate stabilizes could multiply moving parts.
- Mitigation: keep FFI/library migration as an explicit follow-on phase after MVP lowering and equivalence coverage.

### Risk 4: Partial operator coverage

- If unsupported relations fail unclearly, the feature will feel brittle.
- Mitigation: gate MVP support to the relations already implemented in `buildLogicGoal()` and emit direct compiler errors.

## Verification Plan

- Keep existing `logic_run !` object-form tests green.
- Keep existing `logic { ... }` JSON-body tests green.
- Add native-syntax equivalence tests for:
  - single-result membership via `logic(...)`,
  - equivalence between `logic(...)` and `[...] logic!` lowering for the same query,
  - where-less `conde(...)` lowering,
  - contradictory query returning empty results,
  - multi-variable results,
  - explicit limit,
  - later, rewrite-hosted DSL experiments once Phase 5 begins.

## Recommended First Acceptance Slice

The first slice should stop at call-form native logic without capability migration. If the compiler can lower:

- `fresh(...)`,
- supported relation calls,
- `results(...)`,
- and `limit(...)`,

into the current object form with passing equivalence tests, then the feature is already valuable and reviewable.

## Bottom Line

Native relational syntax begins as a compiler-lowering project, not a VM rewrite. Done carefully, it gives humans a better language for expressing queries while preserving the same runtime object model, the same unified literal philosophy, and the current backend in the MVP. `logic(...)` should be the clean primary surface, `[...] logic!` should remain the conceptual anchor, and later rewrite-hosted DSLs plus library/FFI migration can build on that substrate instead of replacing it prematurely.
