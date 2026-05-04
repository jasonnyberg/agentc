# G046 - Continuation-Based Speculation

## Status: COMPLETE

## Parent Context

- Parent proposal source: 🔗[`AgentCLanguageEnhancements-2026-03-22.md`](../../WorkProducts/AgentCLanguageEnhancements-2026-03-22.md)
- Related earlier runtime investigation note: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md) (completed goal summary for G006 — Speculation & Transactions)
- Related concept: 🔗[`UnifiedLiteralModel`](../../Concepts/UnifiedLiteralModel/index.md)

## Goal

Replace the current separate-VM implementation of `speculate [ ... ]` with an in-VM continuation-based execution model so speculative execution uses the same reversible substrate as transactions, avoids whole-VM cloning inside opcode execution, and becomes a direct foundation for later planner/search constructs.

## Why This Matters

- `speculate` is one of AgentC's flagship cognitive features, but today `op_SPECULATE()` is implemented as a workaround that clones a second VM because the active execute loop is not re-entrant.
- That implementation breaks the otherwise clean story that rewrite, logic, transactions, and speculation all sit on one reversible substrate.
- A continuation/frame-based execution core would simplify the mental model: speculative execution becomes "run a nested frame under a checkpoint, capture a result, rollback, continue".
- This is the clearest runtime improvement that sharpens AgentC's identity without widening the surface language much.

## Evidence Snapshot

**Claim**: Current `speculate` semantics are conceptually right but implemented through a re-entrancy workaround.
**Evidence**:
- Primary: `edict/edict_vm.cpp:1418` documents that `op_SPECULATE()` clones a separate `EdictVM` because restoring `VMRES_CODE` inside the running execute loop would jump back to the `SPECULATE` opcode.
- Primary: `edict/edict_vm.cpp:603` shows host transactions already checkpoint most VM resources and allocator watermarks.
- Primary: `edict/edict_compiler.cpp:408` shows surface syntax is already intentionally compact: `speculate [literal]` compiles directly to `VMOP_SPECULATE`.
**Confidence**: 97%

## Expected System Effect

- `speculate` keeps the same user-facing behavior for success/failure, but becomes cheaper and more internally coherent.
- Nested or repeated speculation stops paying for whole-VM clone/copy of roots on every opcode invocation.
- The VM gains a reusable continuation/frame substrate that can later support bounded planner constructs, resumable evaluation, and better explanation traces.

## Work Product

- Detailed plan: 🔗[`ContinuationBasedSpeculationPlan-2026-03-22.md`](../../WorkProducts/ContinuationBasedSpeculationPlan-2026-03-22.md)

## Implementation Checklist

- [x] Confirm runtime path against current VM code/resource stack behavior.
- [x] Extract reusable nested execution path from the main execute loop.
- [x] Add transaction checkpoint mode that preserves the active code-resource stack during rollback.
- [x] Reimplement `op_SPECULATE()` as in-VM checkpoint -> nested execute -> snapshot -> rollback -> materialize.
- [x] Add regression coverage for success, failure, and nested speculative mutation rollback.
- [x] Add deeper hardening coverage for nested speculation plus rewrite/closure isolation.
- [x] Update language/runtime docs to explain the in-VM speculation model.

## Implementation Progress (2026-03-23)

- `op_SPECULATE()` no longer clones a second `EdictVM`; it now compiles the speculative literal, starts a transaction that preserves `VMRES_CODE`, runs nested bytecode in the current VM, snapshots the speculative result, rolls back, and materializes the result back into the caller VM.
- The execute loop has been factored through `runCodeLoop(...)` plus `executeNested(...)`, which exposes the existing code-frame stack as the reusable continuation substrate for nested evaluation.
- Transaction checkpoints now support a `restoreCodeResource` mode so rollback can preserve the currently running code stack when speculation is invoked from inside execution.
- Validation now includes rewrite-rule isolation, closure-driven mutation isolation, and nested-speculation isolation tests, plus a full `ctest` pass (`7/7`).

## Retirement Note

- 2026-04-04: Retired from the active goal list in favor of 🔗[`G052-RuntimeBoundaryHardening`](../G052-RuntimeBoundaryHardening/index.md). No open implementation items remain; only general runtime-boundary preservation carries forward.

## Scope

### In Scope

- Rework execution state so code/resource pointers can be nested without cloning a second VM.
- Preserve current `speculate [ ... ]` surface syntax and observable semantics.
- Define a safe checkpoint/rollback path for speculative nested execution.
- Add tests for parity, nesting, rollback isolation, and planner-oriented repeated probing.

### Out of Scope

- Full planner-block implementation.
- Transaction delta redesign.
- New public syntax beyond preserving current `speculate [ ... ]` semantics.

## Proposed Phases

1. Define continuation/frame data structures and isolate code-resource state from global single-frame execution assumptions.
2. Introduce nested execution of compiled bytecode within the same VM.
3. Reimplement `op_SPECULATE()` in terms of checkpoint -> nested execute -> snapshot -> rollback -> resume.
4. Add regression coverage for behavior parity, nesting, and failure isolation.
5. Document the new runtime model in the language reference and relevant runtime notes.

## Success Criteria

- `speculate [ ... ]` no longer instantiates a second `EdictVM` from inside `op_SPECULATE()`.
- Existing speculation behavior remains intact for success/failure and rollback isolation.
- Nested speculation works without infinite-loop or code-pointer corruption.
- Existing test suites remain green, with new focused speculation regression coverage added.

## Acceptance Signals For Implementation Kickoff

- The plan identifies a smallest safe slice that preserves current language behavior.
- The plan shows how execution-frame state maps to current `instruction_ptr`, `code_ptr`, `code_size`, and related VM fields.
- The plan includes examples that demonstrate why the change matters beyond raw performance.

## Navigation Guide for Agents

- Load the linked work product before implementation; it contains the target model, phased rollout, examples, and risk analysis.
- Re-read `edict/edict_vm.cpp` around transaction checkpoints and `op_SPECULATE()` before touching runtime code.
- Keep the language surface stable; this goal is about runtime coherence, not syntax expansion.
