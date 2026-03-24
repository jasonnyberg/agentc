# Logic Capability Migration Plan

## Historical Status

This document is preserved as a migration log. Earlier sections describe intermediate stages that were later completed and then superseded, including temporary `logic(...)` lowering, `[...] logic!` canonicalization work, builtin `logic` / `logic_run` aliases, and the transitional `VMOP_LOGIC_RUN` path.

Current landed state:

- miniKanren is exposed as imported capability functionality over canonical object/Listree specs,
- higher-level syntax belongs in ordinary Edict wrappers,
- compiler-native logic lowering, `logic { ... }` compiler sugar, and `VMOP_LOGIC_RUN` have all been removed.

Read this file as implementation history plus rationale. For current guidance, prefer `LocalContext/Knowledge/Goals/G047-NativeRelationalSyntax/index.md`, `LocalContext/Knowledge/Goals/G048-LibraryBackedLogicCapability/index.md`, and `LocalContext/Knowledge/WorkProducts/edict_language_reference.md`.

## Objective

Turn Edict logic into a canonical literal-plus-evaluator capability by making `[...] logic!` a first-class execution path, lowering `logic(...)` through that same path, and then moving miniKanren execution behind a reusable library/FFI-backed evaluator boundary.

## Current Baseline

- `logic(...)` already improves readability, but it is still compiled by dedicated lowering structs in `edict/edict_compiler.cpp` and ends by emitting `VMOP_LOGIC_RUN` directly.
- `op_LOGIC_RUN()` still performs three responsibilities at once: query validation, conversion from object spec into `agentc::kanren::Goal`, and execution via `agentc::kanren::run(...)`.
- The object IR (`fresh`, `where`, `conde`, `results`, `limit`) is already stable enough to serve as the machine-facing capability contract.
- `[...] logic!` is documented as the conceptual equivalent of `logic(...)`, but it is not yet the canonical implemented route.

## Target Architecture

### Canonical semantic model

- Data form: object-shaped query spec stored as ordinary Listree values.
- Evaluator form: `logic!` consumes that spec and produces ordinary Listree results.
- Surface sugar: `logic(...)` lowers into the same spec-plus-`logic!` flow.

That means these should be explainable as one thing:

```edict
{"fresh": ["q"], "where": [["membero", "q", ["tea", "cake"]]], "results": ["q"]} logic!
```

```edict
[
  fresh(q)
  membero(q [tea cake])
  results(q)
] logic!
```

```edict
logic(
  fresh(q)
  membero(q [tea cake])
  results(q)
)
```

## Why This Direction Is Better

- It aligns the implementation with Edict's literal-first model instead of leaving logic as compiler/VM privilege.
- It preserves the current object IR for tests, tooling, caching, and future persistence.
- It creates a narrow seam where the current VM-owned miniKanren bridge can later be replaced with a library-backed implementation.
- It avoids the risky first step of turning `fresh`, `conde`, and unification into ad hoc raw FFI calls before a stable evaluator contract exists.

## Recommended Implementation Strategy

### Phase 1 - Make `[...] logic!` real and canonical

Goal:
- Directly support `logic!` on object-shaped query specs and add explicit equivalence tests.

Notes:
- The fastest first slice may simply alias `logic!` to the same backend currently used by `logic_run !` / `VMOP_LOGIC_RUN`.
- If `logic!` already dispatches through ordinary evaluation on imported/builtin callables, prefer exposing a `logic` evaluator object/capsule rather than teaching the compiler another special rule.

Acceptance examples:

```edict
{"fresh": ["q"], "where": [["membero", "q", ["tea", "cake"]]], "results": ["q"]} logic!
```

```edict
[
  fresh(q)
  conde(
    ==(q 'tea)
    ==(q 'coffee)
  )
  results(q)
] logic!
```

### Phase 2 - Lower `logic(...)` into the same evaluator path

Goal:
- `logic(...)` should stop emitting `VMOP_LOGIC_RUN` directly.
- Instead it should compile to the canonical literal representation followed by `logic!`.

Practical effect:
- The compiler still parses native call-form syntax, but the runtime story becomes uniform.
- Tests can compare `logic(...)` and `[...] logic!` directly rather than only comparing them conceptually.

### Phase 3 - Introduce a library-facing evaluator API

Goal:
- Split the existing `op_LOGIC_RUN()` implementation into a thin adapter plus reusable logic-evaluator code.

Recommended boundary:

```cpp
LogicEvalResult evaluateLogicSpec(CPtr<ListreeValue> spec);
```

Responsibilities of that layer:
- validate spec shape,
- build logic variables and goals,
- run the query,
- materialize results as `ListreeValue`.

What stays in VM initially:
- stack interaction,
- error plumbing,
- calling the evaluator.

### Phase 4 - Move evaluator ownership out of the VM core

Goal:
- Relocate the evaluator implementation into a reusable library/service layer that can be called from the VM, and later via curated/imported native capability boundaries.

Recommended first shape:
- one evaluator capability like `kanren.eval(spec)` or `logic.eval(spec)`.

Why not per-relation FFI first:
- `fresh`, `conde`, and goal composition are higher-order search constructs, not simple leaf functions.
- A single evaluator boundary preserves correctness and keeps ownership/effect semantics manageable.

### Phase 5 - Optional deeper externalization

Possible later work:
- expose logic goal builders as ordinary capabilities,
- allow rewrite-hosted DSLs to normalize to canonical query specs,
- add capability metadata / explainability for logic plans and results.

This is explicitly later work, not part of the first migration slice.

## Concrete Examples

### Example A - Current native syntax redirected to canonical evaluator

Desired compiled meaning:

```edict
logic(
  fresh(q)
  membero(q [tea cake jam])
  results(q)
)
```

becomes conceptually:

```edict
{"fresh": ["q"], "where": [["membero", "q", ["tea", "cake", "jam"]]], "results": ["q"]} logic!
```

System effect:
- one evaluator path,
- easier equivalence testing,
- easier future migration of the evaluator implementation.

### Example B - Grouped `conde(...)` through `[...] logic!`

```edict
[
  fresh(q category)
  conde(
    all(
      ==(q 'tea)
      ==(category 'hot)
    )
    all(
      ==(q 'coffee)
      ==(category 'iced)
    )
  )
  results(q category)
] logic!
```

System effect:
- proves the literal-evaluator story handles the richer G047 syntax too,
- keeps the backend IR unchanged,
- removes pressure to preserve direct compiler-to-opcode shortcuts forever.

### Example C - Library-backed evaluator under the same surface

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

Phase-3/4 implementation effect:
- source stays the same,
- `logic(...)` still lowers to a spec,
- `logic!` delegates through a reusable evaluator API,
- the evaluator implementation can move out of `EdictVM` without changing source-level semantics.

## Risks And Controls

- Risk: two parallel evaluator paths survive too long.
  - Control: make `logic(...)` target `logic!` as soon as feasible.
- Risk: capability migration is interpreted as “every relation must be raw FFI now.”
  - Control: explicitly stage around one evaluator boundary first.
- Risk: compiler-specific structs become permanent semantic infrastructure.
  - Control: keep `NativeLogicTerm` and related types limited to syntax lowering or replace them later with generic spec builders.
- Risk: error reporting gets worse after refactoring.
  - Control: preserve compiler source locations in lowering and return structured evaluator errors.

## Verification Plan

- Add equivalence tests for direct object-spec `logic!` evaluation.
- Add equivalence tests showing `logic(...)` and `[...] logic!` return the same answers.
- Preserve all existing G047 logic tests.
- Add adapter-level tests for the extracted evaluator API before moving ownership outside the VM file.
- Keep full `ctest` green at each phase.

## Recommended First Acceptance Slice

1. Add tests for object-spec `logic!` and native-literal `[...] logic!`.
2. Implement `logic(...) -> canonical spec -> logic!` lowering.
3. Refactor `op_LOGIC_RUN()` into a thin adapter around an internal evaluator helper.

This slice delivers the architectural turning point without prematurely forcing a full FFI redesign.

## Implementation Status - 2026-03-23

### What landed

- `logic` is now registered as a builtin evaluator alias to `VMOP_LOGIC_RUN` alongside existing `logic_run` compatibility in `edict/edict_vm.cpp`.
- `EdictCompiler` now exposes `compileNativeLogicSpecBody(const std::string&)` so native logic literal bodies can be parsed into the canonical object-spec bytecode without immediately evaluating them.
- `logic(...)` no longer emits `VMOP_LOGIC_RUN` directly; it now builds the canonical object spec and then emits an ordinary evaluator call through `logic`.
- `op_LOGIC_RUN()` now normalizes either an object-shaped query spec or a scalar native logic literal body into the same object-spec path before running the existing miniKanren bridge.
- The spec-validation / goal-building / `kanren::run(...)` bridge now lives in reusable helper `kanren/logic_evaluator.cpp` behind `agentc::kanren::evaluateLogicSpec(...)`, and `op_LOGIC_RUN()` has been reduced to normalization plus adapter behavior.
- Direct test coverage now proves the extracted evaluator can consume canonical object specs outside the VM opcode path.
- Ownership of that reusable evaluator has now moved into the `kanren/` library itself, which better matches the architecture that Edict constructs specs while the logic library owns evaluation.
- A first imported/library-facing capability boundary now exists in `kanren/runtime_ffi.h` and `kanren/runtime_ffi.cpp`: `agentc_runtime_ctx*` owns opaque `agentc_value` handles, and `agentc_logic_eval(...)` exposes logic evaluation through that boundary while bridging to and from `LTV` handles.
- Edict-side imported-capability coverage now exercises that ABI through the normal Cartographer resolve/import path using a dedicated test header plus `resolver.import_resolved !` against `libkanren.so`.
- Pure Edict wrappers now prove the canonical-spec path can be expressed as ordinary Edict code: `CallbackTest.PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren` defines wrappers such as `fresh(x)`, `results(x)`, `membero(x y)`, a fixed-arity pair helper, `logic_spec(...)`, and `logic_eval(...)`, builds the canonical spec in Edict, verifies its shape, and evaluates it through imported `libkanren.so` without using builtin logic syntax.
- The detachment slice is now landed: `logic` / `logic_run` are no longer VM builtins, `op_LOGIC_RUN()` was reduced to a legacy stub during transition and has now been removed entirely, `libedict` no longer links directly against `kanren`, and imported convenience entrypoint `agentc_logic_eval_ltv(LTV spec)` now exposes canonical logic evaluation through pure import/FFI when Edict already has an `ltv` value.
- Edict-side logic tests now prove the imported-only path works end-to-end by importing `libkanren.so` in test preludes and aliasing `logic` / `logic_run` to `logicffi.agentc_logic_eval_ltv`.
- The final cleanup slice is now landed: compiler-native logic lowering has been removed from `edict/edict_compiler.cpp`, including `compileNativeLogicCall()`, `compileNativeLogicSpecBody()`, and the compiler-only `NativeLogic*` parser/lowering structs, so `logic(...)` is no longer a privileged compiler form.
- `VMOP_LOGIC_RUN` has been removed completely from the opcode enum, VM declaration/dispatch, and regression-matrix classification, so no VM-owned logic opcode remains in code.
- A final post-detachment cleanup has now removed `logic { ... }` from the compiler as well; surviving logic paths are imported object-spec evaluation and ordinary Edict wrapper-based sugar such as those proven in `CallbackTest.PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren`.

### Validation completed

- `LogicSurfaceTest.ObjectSpecWorksThroughLogicEvaluatorAlias`
- `LogicSurfaceTest.LibraryEvaluatorConsumesCanonicalObjectSpec`
- `KanrenTest.RuntimeAbiEvaluatesLogicSpecThroughOpaqueHandles`
- `CallbackTest.ImportResolvedKanrenRuntimeAbiEvaluatesLogicSpec`
- `CallbackTest.PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren`
- `CognitiveValidationTest.TransactionRollbackContainsLogicAndRewriteExecution`
- Focused detached-logic coverage remained green after removing compiler-native lowering: `LogicSurfaceTest.*`, `CallbackTest.PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren`, and `CognitiveValidationTest.TransactionRollbackContainsLogicAndRewriteExecution` all passed.
- Focused detached-logic coverage remained green after removing `logic { ... }` compiler support and replacing the remaining non-duplicative tests with imported object-spec flows.
- Full `ctest` passed: `7/7` targets green.

### Remaining follow-up

- G048 implementation work is complete.
- Future follow-up, if desired, is ergonomic only: package reusable wrapper libraries or rewrite-hosted sugar on top of the imported object-spec evaluator path.

## Bottom Line

This migration path has now been completed: miniKanren is exposed as imported capability functionality over canonical object/Listree specs, and any higher-level syntax is expected to live in ordinary Edict wrappers rather than compiler or VM special cases.
