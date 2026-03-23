# G048 - Library-Backed Logic Capability

## Status: COMPLETE

## Parent Context

- Parent goal: 🔗[`G047-NativeRelationalSyntax`](../G047-NativeRelationalSyntax/index.md)
- Parent proposal source: 🔗[`AgentCLanguageEnhancements-2026-03-22.md`](../../WorkProducts/AgentCLanguageEnhancements-2026-03-22.md)
- Related concept: 🔗[`UnifiedLiteralModel`](../../Concepts/UnifiedLiteralModel/index.md)

## Goal

Make Edict logic evaluation look like an ordinary capability over literal data by establishing `[...] logic!` as a first-class form, lowering `logic(...)` through that same form, and progressively moving miniKanren execution behind a library/FFI-backed evaluator boundary instead of keeping the semantics owned directly by `VMOP_LOGIC_RUN`.

## Why This Matters

- G047 improved human-facing syntax, but the semantic entrypoint is still VM-owned because native logic still compiles straight to `VMOP_LOGIC_RUN`.
- The language philosophy is stronger if `logic(...)` and `[...] logic!` are two views of the same literal-first model rather than separate surfaces with different backend paths.
- A capability boundary is the cleanest route toward shrinking VM ownership of miniKanren semantics without regressing the working feature set.
- Moving the evaluator behind a library-facing API also creates a better foundation for future rewrite-hosted DSL sugar and capability inspection.

## Evidence Snapshot

**Claim**: The current implementation has syntax-level progress but still keeps logic execution in compiler/VM internals.
**Evidence**:
- Primary: `edict/edict_compiler.cpp:588`-`edict/edict_compiler.cpp:714` shows `logic(...)` is parsed by dedicated compiler-only structs (`NativeLogicTerm`, `NativeLogicGoal`, `NativeLogicQuery`) and still emits `VMOP_LOGIC_RUN` directly.
- Primary: `edict/edict_vm.cpp:3015`-`edict/edict_vm.cpp:3069` shows `op_LOGIC_RUN()` still converts the object spec into `agentc::kanren::Goal` values and directly calls `agentc::kanren::run(...)`.
- Primary: `LocalContext/Knowledge/Goals/G047-NativeRelationalSyntax/index.md:13` already states the intended long-term direction is migration toward library/FFI-backed capability layers.
**Confidence**: 96%

## Expected System Effect

- `logic(...)` becomes clean sugar over the same literal-plus-evaluator path as `[...] logic!`.
- The compiler can stop owning as much logic-specific semantic knowledge and focus on lowering syntax into ordinary data.
- The VM can shrink from "logic engine host" toward "logic capability adapter".
- A later imported module or curated native library can provide logic evaluation while preserving the current object IR and test behavior.

## Work Product

- Detailed plan: 🔗[`LogicCapabilityMigrationPlan-2026-03-23.md`](../../WorkProducts/LogicCapabilityMigrationPlan-2026-03-23.md)

## Implementation Checklist

- [x] Define the canonical literal shape consumed by `logic!` and document exact equivalence with `logic(...)`.
- [x] Add direct coverage for `[...] logic!` using the current object-shaped query form.
- [x] Change `logic(...)` lowering so it targets the same literal-plus-`logic!` path instead of emitting `VMOP_LOGIC_RUN` directly.
- [x] Refactor `op_LOGIC_RUN()` into a thin adapter around a library-facing evaluator API.
- [x] Extract current spec-to-goal / run logic into a reusable logic-evaluator layer that can live outside the VM core.
- [x] Define the first library/FFI-backed capability boundary (likely a single evaluator entrypoint rather than per-relation FFI calls).
- [x] Remove `logic { ... }` compiler compatibility after imported/object-spec coverage made it redundant.
- [x] Validate semantic equivalence across object IR, `[...] logic!`, and `logic(...)`.

## 2026-03-23 Progress

- Landed the first G048 implementation slice in code: `logic` is now a builtin evaluator alias alongside `logic_run`, `logic(...)` lowers through the canonical evaluator path instead of emitting `VMOP_LOGIC_RUN` directly, and `op_LOGIC_RUN()` now accepts either the existing object-shaped spec or a native logic literal body that it canonicalizes into that same spec.
- Added direct equivalence coverage in `edict/tests/logic_surface_test.cpp` for object-spec `logic!`, native literal `[...] logic!`, and `logic(...)` returning the same answers.
- Validation is green for the slice: focused native-logic gtests passed, full `edict_tests` passed (`85 tests from 13 test suites`), and full `ctest` passed (`7/7`).
- Landed the second G048 slice in code: the spec-validation / goal-building / `kanren::run(...)` bridge now lives in reusable helper `kanren/logic_evaluator.cpp` with public entrypoint `agentc::kanren::evaluateLogicSpec(...)`, and `op_LOGIC_RUN()` has been reduced to normalization plus adapter behavior.
- Added direct coverage in `edict/tests/logic_surface_test.cpp` proving the reusable evaluator can consume a canonical object spec outside the VM opcode path.
- Landed the third G048 refactor slice in code: evaluator ownership has now moved from the `edict/` subtree into the `kanren/` library itself, so the reusable Listree-spec bridge now lives with the logic engine rather than the VM-facing layer.
- Validation remains green after the move: focused logic-evaluator tests passed and full `ctest` passed (`7/7`).
- Landed the fourth G048 slice in code: `kanren/runtime_ffi.h` and `kanren/runtime_ffi.cpp` now expose the first imported/library-facing capability boundary with a generalized `agentc_runtime_ctx*`, opaque `agentc_value` handles, `agentc_runtime_value_from_ltv(...)`, `agentc_runtime_value_to_ltv(...)`, `agentc_runtime_copy_last_error(...)`, and `agentc_logic_eval(...)`.
- Added direct runtime-ABI coverage in `kanren/tests/kanren_tests.cpp`, proving a canonical Listree query spec can be imported into a runtime context, evaluated through `agentc_logic_eval(...)`, and materialized back out through the opaque-handle boundary.
- Validation remains green after the ABI slice: focused logic tests passed and full `ctest` passed (`7/7`).
- Landed the fifth G048 slice in code: Edict-side imported-capability coverage now exercises the new `kanren/runtime_ffi.cpp` boundary through the normal Cartographer resolver/import path using `resolver.import_resolved !` and imported calls such as `logicffi.agentc_runtime_create !`, `logicffi.agentc_runtime_value_from_ltv !`, `logicffi.agentc_logic_eval !`, and `logicffi.agentc_runtime_value_to_ltv !`.
- Added `CallbackTest.ImportResolvedKanrenRuntimeAbiEvaluatesLogicSpec`, which resolves and imports `libkanren.so` from a dedicated test header and proves a canonical query spec can cross the imported FFI path and return the expected `tea`, `cake` results inside Edict.
- Validation remains green after the integration slice: focused imported-capability and logic-surface tests passed and full `ctest` passed (`7/7`).
- Landed the sixth G048 slice in code: pure Edict wrapper forms now prove the language can build canonical logic specs without builtin logic syntax and then evaluate them through the imported `kanren` capability. `CallbackTest.PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren` defines wrapper thunks such as `fresh(x)`, `results(x)`, `membero(x y)`, a fixed-arity list helper, `logic_spec(...)`, and `logic_eval(...)`, then verifies the wrapper-built canonical spec and its imported evaluation result.
- Validation remains green after the wrapper slice: focused imported logic tests passed and full `ctest` passed (`7/7`).
- Landed the seventh G048 slice in code: kanren evaluation is no longer VM-owned by default. `logic` and `logic_run` were removed from `loadCoreBuiltins()`, `compileLogicBlock()` now emits the ordinary evaluator call path instead of `VMOP_LOGIC_RUN`, `op_LOGIC_RUN()` is now only a legacy error stub, and `libedict` no longer links against `kanren` directly.
- Added direct imported-logic convenience in `kanren/runtime_ffi.h` / `.cpp` via `agentc_logic_eval_ltv(LTV spec)`, making pure imported evaluation possible without the runtime-context handle API when Edict already has an `ltv` value.
- Updated Edict-side tests so logic execution now works through imported capability setup rather than VM builtins: `logic_surface_test.cpp` imports `libkanren.so` and aliases `logic` / `logic_run` to the imported `agentc_logic_eval_ltv`, `cognitive_validation_test.cpp` now does the same for transactional logic coverage, and callback/import tests were updated to exercise the direct imported LTV path.
- Validation remains green after the detachment slice: focused imported logic tests passed and full `ctest` passed (`7/7`).
- Landed the eighth and final G048 slice in code: compiler-native logic lowering has now been removed from `EdictCompiler`. The dedicated `compileNativeLogicCall()`, `compileNativeLogicSpecBody()`, and compiler-only `NativeLogic*` parsing/lowering structs are gone, so `logic(...)` is no longer privileged compiler syntax and pure Edict wrapper construction is the remaining path for higher-level logic sugar.
- Retired the legacy VM opcode completely: `VMOP_LOGIC_RUN` has been removed from `edict/edict_types.h`, `edict/edict_vm.h`, `edict/edict_vm.cpp`, and the regression-matrix heavy-op list, leaving no VM-owned kanren execution path behind.
- Updated detached-logic validation to keep the imported-capability path green after the final removal: focused `LogicSurfaceTest.*`, `CallbackTest.PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren`, and `CognitiveValidationTest.TransactionRollbackContainsLogicAndRewriteExecution` all passed, and full `ctest` remained green (`7/7`).
- Landed a final cleanup pass after goal completion: removed the last compiler special-case for `logic { ... }` from `EdictCompiler`, converted the remaining non-duplicative tests to imported object-spec flows, and confirmed focused detached-logic coverage plus full `ctest` still pass.
- Goal complete: kanren capabilities are now detached from the VM, available through import/FFI, and no longer depend on compiler-native lowering or a legacy logic opcode.

## Scope

### In Scope

- Canonicalize `[...] logic!` as a real, tested evaluator path.
- Redirect `logic(...)` to that same path.
- Introduce a reusable evaluator abstraction between VM and miniKanren execution.
- Plan and stage migration toward a library/FFI-backed logic capability while preserving current behavior.

### Out of Scope

- Replacing miniKanren semantics during the first migration slice.
- Requiring each relation (`membero`, `conde`, `==`, etc.) to become an independent imported FFI function immediately.
- Broad redesign of the logic IR.
- Rewrite-hosted DSLs as part of the first capability migration slice.

## Proposed Phases

1. Canonicalize the literal-evaluator model: object IR and `[...] logic!` become the explicit semantic core.
2. Make `logic(...)` lower into that core rather than into `VMOP_LOGIC_RUN` directly.
3. Introduce a small evaluator API that accepts a query spec and returns Listree-backed answers.
4. Move the current miniKanren bridging code out of `EdictVM` into a reusable library-facing layer.
5. Add a curated/imported logic capability entrypoint with the same semantics.
6. Revisit whether deeper decomposition of relations into ordinary capabilities is worthwhile after the evaluator boundary is stable.

## Success Criteria

- `logic(...)`, `[...] logic!`, and direct object-form logic all execute through one explainable semantic path.
- The VM no longer owns the full logic translation/execution flow directly.
- The first capability boundary is small and testable.
- Existing logic behavior and tests remain stable during the migration.
- The resulting architecture clearly supports later library/FFI-backed logic without forcing premature per-relation FFI exposure.

## Navigation Guide for Agents

- Treat G048 as the architecture follow-on to G047, not a replacement for it.
- Prefer one evaluator capability over many tiny FFI relations in the first slice.
- Preserve the object IR as the stable machine form while making `[...] logic!` a real runtime path.
- Keep the unified literal model central: logic should feel like data plus evaluation, not a privileged sublanguage.
