# WP — G100 Edict Isolation Contract Hardening

**Goal**: G100 — Edict Isolation Contract Hardening
**Status**: Complete
**Date**: 2026-06-20

## Summary

G100 hardens the documented Edict isolation contract for LLM-generated helper code. The verified boundary is:

- `f(args...)` creates an isolated function/data/dictionary frame.
- Arguments and thunk-body local assignments stay inside that isolated frame.
- Values left on the isolated frame's data stack merge back to the caller at `FUN_POP`.
- Parent bindings are not overwritten by plain `@name` inside isolated calls.
- New bindings created inside isolated calls do not leak to the parent dictionary.
- Deliberate object mutation remains available through explicit context entry: `ctx < ... > /`.

This supports direct Edict generation after G101 by giving model-emitted helper snippets a safer default execution shape: use isolated calls for helper transforms, and use `< ... > /` only when intentionally mutating a target object.

## Contract Examples

### Isolated call protects parent binding

```edict
[parent] @x
[[child] @x x] @mutate
mutate() @result
x print          -- parent
```

`mutate()` returns `child`, but parent `x` remains `parent`.

### Isolated call does not leak new binding

```edict
[[child] @new_binding new_binding] @create_binding
create_binding() @result
new_binding print -- unresolved symbol fallback
```

### Explicit context entry mutates target object

```edict
{} @ctx
ctx < [child] @x > /
ctx.x print -- child
```

The trailing `/` discards the context object returned by `CTX_POP`.

## Test Coverage Matrix

| Test | Verifies |
|---|---|
| `CallIsolationTest.StackIsolation` | Bare stack operations inside `f()` cannot consume caller stack items. |
| `CallIsolationTest.LocalScopeIsolation` | Bindings made while evaluating call arguments stay local. |
| `CallIsolationTest.IsolatedCallCannotOverwriteParentBindingFromThunkBody` | Thunk-body `@x` inside `f()` cannot overwrite parent `x`; return values still merge. |
| `CallIsolationTest.IsolatedCallCannotLeakNewBindingFromThunkBody` | Thunk-body `@new_binding` inside `f()` does not create a parent binding. |
| `CallIsolationTest.ExplicitContextEntryMutatesTargetObject` | `ctx < ... > /` deliberately mutates the target object. |
| `CallIsolationTest.ResultMerging` | Results left by an isolated call merge back to the parent stack. |
| `CallIsolationTest.NestedCalls` | Nested isolated frames preserve stack isolation. |
| `CallIsolationTest.OneShotListAssignment` | Isolated calls can still produce structured list results. |

## Documentation Updates

Updated `WP-LlmsGuideToEdictVm-2026-05-10/index.md` with:

- test-backed isolated-call safety examples;
- explicit context-entry mutation example;
- guidance for LLM-generated helper code;
- compact direct-Edict vs host JSON tool-envelope example using current syntax.

## Validation

Passed:

- `cmake --build build --target edict_tests -j$(nproc)`
- `./edict/edict_tests --gtest_filter='CallIsolationTest.*'` — 8/8 passed.
- `git diff --check`

Broader suite note:

- Full `./edict/edict_tests` currently reports 5 failures in unrelated ReproFFI/Callback suites:
  - `ReproFFITest.AddPoC`
  - `CallbackTest.ClosureFromParamSignature`
  - `CallbackTest.EdictBuiltinsMapLoadAndInvokeCartographerFunction`
  - `CallbackTest.ParserMapCanRoundTripThroughJson`
  - `CallbackTest.PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren`
- These failures are outside the G100 call-isolation surface and were confirmed by the focused failing filter `ReproFFITest.*:CallbackTest.*`.

## Relationship To Other Goals

- Parent: G078 — Edict-Resident Agent Loop Consolidation.
- Complements: G101 — Direct Edict Tool-Emission Path.
- Next backlog goal after G100: G094 — Curated Native Cognitive Capability Libraries.

## Future Work

- If later goals allow arbitrary model-emitted source beyond guarded direct actions, add a dedicated execution sandbox policy and negative tests for native capability access.
- If strict lookup becomes the default for generated snippets, add companion tests proving missing-symbol failures are data-shaped and recoverable.
