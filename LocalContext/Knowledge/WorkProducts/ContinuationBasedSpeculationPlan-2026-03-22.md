# Continuation-Based Speculation Plan

## Objective

Rework Edict execution so `speculate [ ... ]` runs as a nested continuation/frame inside the current VM rather than by cloning a separate `EdictVM`. The surface language should stay the same; the runtime should become more coherent with AgentC's reversible-state model.

## Current Baseline

### Observable behavior today

- `speculate [code]` executes code in isolation.
- On success, the speculative result is copied back into the host VM.
- On failure, the host VM receives `null` and speculative side effects disappear.

### Implementation reality today

- `beginTransaction()` / `rollbackTransaction()` already checkpoint allocator watermarks, rewrite rules, resource roots, and key execution state in the host API path.
- `op_SPECULATE()` cannot use that mechanism from inside the main execute loop because code-resource rollback would restore the currently executing frame back onto the `SPECULATE` opcode itself.
- The current workaround is to clone a second VM, copy most roots into it, compile the literal there, execute it there, snapshot its result, and materialize the snapshot back in the host VM.

## Why Change It

### Conceptual rationale

- AgentC's pitch is one reversible cognitive substrate. A special "spawn another VM" path inside the flagship speculative operator weakens that story.
- If transactions, rewrite, logic, and speculation are all manifestations of reversible execution, they should share the same frame/checkpoint model.

### Practical rationale

- Whole-VM cloning copies more state than the user thinks about.
- Repeated speculative probes become an avoidable runtime tax.
- Planner-style features will be easier to build on explicit continuations than on ad hoc VM cloning.

## Design Constraints

- Preserve the existing `speculate [ ... ]` surface syntax.
- Preserve current success/failure semantics.
- Preserve isolation: speculative mutations must not leak unless explicitly materialized as return values.
- Keep the change compatible with the unified literal model: `speculate` remains an evaluation form over existing literals, not a new syntax domain.

## Proposed Target Model

### 1. Execution frames become explicit

Replace the most fragile global execution fields with an explicit frame record, for example:

```cpp
struct ExecutionFrame {
    const uint8_t* code_ptr;
    size_t code_size;
    size_t instruction_ptr;
    bool tail_eval;
    int scan_depth;
};
```

The VM then owns a small frame stack or continuation stack, with the active frame at the top.

### 2. Nested execute becomes ordinary

Instead of assuming one global code resource, the VM should be able to push a frame for newly compiled bytecode, run it to completion, then pop back to the caller frame.

### 3. `speculate` becomes checkpointed nested execution

Recommended flow:

1. Compile the speculative literal to bytecode.
2. Begin a transaction checkpoint in the current VM.
3. Push a child execution frame for the speculative bytecode.
4. Run until the child frame completes or errors.
5. Snapshot the top speculative result into a persistence-safe/materializable representation.
6. Roll back the checkpoint.
7. Materialize the result into the caller frame and continue execution.

That makes speculation an ordinary combination of frame nesting and rollback.

## Semantics To Preserve

### Example 1: successful isolated probe

Current and target behavior should match:

```edict
'baseline
speculate ['trial]
```

Expected effect:

- outer stack still contains `'baseline`,
- speculation returns `'trial`,
- no speculative stack mutations leak except the returned value.

### Example 2: failing probe

```edict
'baseline
speculate [swap]
```

Expected effect:

- speculative `swap` fails inside the child frame,
- outer VM state is unchanged,
- result pushed to caller is `null`.

### Example 3: speculative dictionary mutation

```edict
[] @session
speculate [session 'draft @mode session]
session
```

Expected effect:

- speculative code can create or modify `session.mode`,
- rollback discards that mutation,
- final `session` in the outer VM remains the original empty object.

### Example 4: repeated probe loop becomes cheaper

```edict
[candidate-a] speculate [validator !]
[candidate-b] speculate [validator !]
[candidate-c] speculate [validator !]
```

Expected system effect:

- user semantics unchanged,
- runtime no longer clones a second VM three times,
- future planner constructs can build on the same frame/checkpoint machinery.

## Recommended Implementation Slices

### Slice A - Frame extraction without semantic change

- Introduce `ExecutionFrame` and move `code_ptr`, `code_size`, `instruction_ptr`, `tail_eval`, and `scan_depth` behind an active-frame accessor.
- Keep one frame initially so behavior is unchanged.
- Add focused runtime tests that prove normal execution still behaves identically.

### Slice B - Nested frame execution helper

- Add an internal helper such as `executeNested(const BytecodeBuffer&)` that pushes a child frame and runs only that frame to completion.
- Do not wire `speculate` to it yet; prove the helper is sound first.
- Verify return to the parent frame leaves outer code position intact.

### Slice C - Reimplement `op_SPECULATE()`

- Replace separate-VM cloning with the sequence: compile -> checkpoint -> nested execute -> capture -> rollback -> materialize.
- Keep fallback behavior simple: on compile or execute error, return `null`.

### Slice D - Hardening and edge coverage

- Add nested speculation tests.
- Add tests proving rewrite rules, dictionary changes, closures, and stack mutations do not leak across rollback.
- Re-run existing regression matrix and transaction/speculation suites.

### Slice E - Explainability follow-up hooks

- Optionally expose frame/checkpoint metadata that a future `explain` facility can use.
- This is not required for the first implementation, but the frame model should leave room for it.

## Code Areas Likely To Change

- `edict/edict_vm.cpp` - execute loop, `op_SPECULATE()`, transaction/frame helpers.
- `edict/edict_vm.h` - frame definitions and helper declarations.
- `edict/tests/transaction_test.cpp` - expanded speculation parity coverage.
- `edict/tests/vm_stack_tests.cpp` - behavior parity for existing syntax-level cases.
- `LocalContext/Knowledge/WorkProducts/edict_language_reference.md` - runtime notes if semantics wording changes.

## Risk Analysis

### Risk 1: Execute loop regressions

- Replacing global execution fields with a frame abstraction touches core VM control flow.
- Mitigation: first land a no-semantic-change refactor that only wraps existing fields in a single-frame model.

### Risk 2: Transaction/frame interaction bugs

- Rollback currently restores execution state directly.
- Mitigation: define exactly which parts of active-frame state belong in checkpoints and add nested-frame tests before switching `op_SPECULATE()`.

### Risk 3: Result materialization gaps

- Current speculative snapshotting only supports certain result shapes.
- Mitigation: treat runtime refactor and richer snapshot semantics as separate concerns; preserve current snapshot behavior first.

## Verification Plan

- Existing speculation syntax tests continue to pass.
- Existing transaction tests continue to pass.
- Add new tests for nested speculation, speculative rewrite-rule isolation, speculative object mutation isolation, and repeated speculation parity.
- Re-run full test suite after the `op_SPECULATE()` switchover.

## Recommended First Acceptance Slice

The first acceptable implementation slice is not the full feature. It is:

1. extract execution-frame state into a struct,
2. preserve single-frame behavior,
3. add one nested-execute helper test,
4. then switch `op_SPECULATE()` only after those pass.

That sequence keeps the riskiest runtime change reviewable and makes rollback of the implementation itself straightforward if problems appear.

## Implementation Status (2026-03-23)

### What landed

- The VM now exposes `runCodeLoop(size_t stopCodeDepth, bool markCompleteOnDrain)` and `executeNested(const BytecodeBuffer& code)` so nested bytecode can run against the existing code-frame stack without spawning a second VM.
- Transaction checkpoints gained a `restoreCodeResource` flag. The speculation path uses `beginTransaction(false)` so rollback restores ordinary VM resources and allocator watermarks without rewinding the active caller code stack.
- `op_SPECULATE()` was rewritten to use the planned flow: compile -> checkpoint -> nested execute -> snapshot -> rollback -> materialize.

### Validation completed

- `VMStackTest.SpeculateLiteralReturnsResultWithoutChangingBaseline`
- `VMStackTest.SpeculateFailureReturnsNullWithoutChangingBaseline`
- `VMStackTest.SpeculateRollsBackNestedMutationInRunningVm`
- `VMStackTest.SpeculateRollsBackRewriteRulesDefinedInsideProbe`
- `VMStackTest.SpeculateRollsBackClosureDrivenMutation`
- `VMStackTest.NestedSpeculateKeepsInnerMutationIsolatedFromOuterProbe`
- Full `ctest` suite passed: `7/7` targets.

### Remaining follow-up

- Add repeated planner-style probe coverage if future planner work starts leaning on nested speculation loops more heavily.

## Bottom Line

This change does not make the language larger. It makes the runtime more honest about what AgentC already claims to be: a reversible execution substrate where speculative execution is a normal property of the VM rather than a second-VM escape hatch.
