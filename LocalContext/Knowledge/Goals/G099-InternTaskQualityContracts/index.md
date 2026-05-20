# Goal: G099 — Intern Task Quality Contracts

**Status**: ACTIVE / FIRST SLICE
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)

## Objective
Define the task-contract layer for local intern agents: which tasks are safe to delegate, how success criteria are expressed, and how outputs are validated before the coordinator trusts them.

## Source Extracted From
Conversation summary section **“Quality constraint”** and the intern delegation taxonomy.

## Rationale
The intern architecture only works if delegated tasks are bounded and checkable. Local models can grep, summarize, extract, and classify; they should not be trusted as autonomous architects over deep multi-file reasoning. The system needs explicit contracts that encode this distinction.

## Candidate Contract Fields
- Task kind: gather, classify, filter, compress, context-proxy.
- Inputs: files, commands, search terms, context node refs, limits.
- Expected output schema.
- Success criteria and validation checks.
- Token/time/resource budget.
- Confidence/evidence fields for returned findings.
- Async-job limits: timeout, max events, max result bytes, expected publication mode, cancellation semantics, waitable id, and broker backpressure behavior.

## Current Priority — Full-Send Slab Plan
G099 follows the broker-shaped async planning in 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md), the async 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md) `intern_start!` / `intern_sync!` / `intern_cancel!` slice, and completed 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](../G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md). The first contract does not need the full future policy layer; it should minimally bound async workers so the coordinator can reject underspecified jobs before they become background processes or broker waitables, and its validation helpers should live in Edict rather than native VM opcode checks.

Minimal first schema target:

```json
{
  "task_id": "...",
  "program": "...",
  "input": {},
  "context": {},
  "imports": {},
  "limits": {
    "timeout_ms": "1000",
    "max_events": "100",
    "max_result_bytes": "65536"
  },
  "expect": {
    "result_shape": "object",
    "success_field": "ok"
  },
  "async": {
    "waitable": null,
    "event_kinds": ["started", "progress", "complete", "error", "cancelled", "backpressure"],
    "backpressure": "reject",
    "cancellation": "cooperative"
  }
}
```

The result envelope should reserve future slab publication metadata even while JSON remains the first stable handoff:

```json
{
  "result": {},
  "publication": null
}
```

## Implementation Plan
- [x] Define a minimal first Listree/JSON task contract schema with `limits` and `expect` fields.
- [x] Add first Edict helpers for validating intern task/status contracts over the completed G111 module-backed surface: `intern.validate_task_contract!` and `intern.validate_status_envelope!` now live in `cpp-agent/edict/modules/intern.edict`.
- [x] Provide examples for each first safe task class: `intern.example_gather_task!`, `intern.example_classify_task!`, and `intern.example_filter_task!` return bounded/checkable task contracts.
- [x] Integrate the contract with the worker dispatch path from 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md): `intern_start!` now rejects malformed/over-broad async tasks before launching a worker or creating an active broker waitable.
- [x] Add first tests that reject underspecified intern tasks and validate public status-envelope shape.

## Implementation Progress — 2026-05-19

Dispatch-integration slice landed for async workers:

- `intern.validate_task_contract!` now requires non-empty `program`, `expect.success_field`, and `limits.max_result_bytes`. Missing result limits are treated as over-broad async task contracts and return `error.code: "missing_result_limit"`.
- `intern_start!` now rejects malformed/over-broad async tasks before launching a worker or creating an active broker waitable. The native start-status primitive mirrors the Edict validator contract at the dispatch boundary so the public module returns the usual intern start envelope shape without adding VM opcodes.
- `intern_run!` remains permissive for now as blocking compatibility sugar; mandatory contract enforcement is currently scoped to async/background dispatch where malformed tasks can otherwise consume worker slots.
- `InternWorkerTest.InternStartEnforcesTaskContractBeforeDispatch` covers missing `expect.success_field` and missing `limits.max_result_bytes`, and verifies active job count remains unchanged.

## Implementation Progress — 2026-05-18

First contract-validation slice landed in `intern.edict`:

- `intern.validate_task_contract!` accepts a task-like object and returns `state: "contract_valid"` when it has a non-empty `program`, `expect.success_field`, and `limits.max_result_bytes`; it preserves `task_id`, `limits`, and `expect` in the returned status.
- Missing required fields return `state: "contract_error"` with structured `error.code` / `error.message`; checked errors include `missing_program`, `missing_success_field`, and `missing_result_limit`.
- `intern.validate_status_envelope!` validates that a G111 public status/envelope has a visible `state` and returns `state: "envelope_valid"` with the envelope state, job/task ids, and publication field.
- `InternWorkerTest.InternContractValidatorsCheckTaskAndStatusShape` covers a valid contract, an underspecified contract, and a valid public status envelope.

## Validation — 2026-05-19
- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.InternExamplesRepresentSafeTaskClasses:InternWorkerTest.InternResultContractRequiresSuccessAndEvidence' --gtest_brief=1` — passed 2/2.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*:Root1AwaitSchedulerTest.*:Root1PrimitiveModuleTest.*:EdictVM.YieldedExecutionCanResumeCurrentCodeFrame' --gtest_brief=1` — passed 19/19 after examples/result-validator work; earlier dispatch slice passed 17/17.

## Progress Notes

### 2026-05-19
- Did: Integrated first contract enforcement with async worker dispatch. `intern_start!` rejects tasks missing `expect.success_field` or `limits.max_result_bytes` before worker launch, while preserving `intern_run!` compatibility for now. Added regression coverage for malformed/over-broad async task rejection and no active-job leak.
- Did: Added safe task-class examples for gather/classify/filter as Edict module words: `intern.example_gather_task!`, `intern.example_classify_task!`, and `intern.example_filter_task!`. Each example carries bounded `limits`, explicit `expect`, empty read-only-safe context/import defaults, and a program that writes `result.ok` plus `result.evidence`.
- Did: Added first result-validation helper surface, `intern.validate_result_contract!`, backed by the worker primitive substrate for robust nested-field checks. It accepts `{ "expect": ..., "envelope": ... }`, returns `state: "result_valid"` when a complete result has success evidence and evidence entries, and rejects missing envelope state/result/success/evidence with structured `result_error` codes.
- Decided: Mandatory enforcement starts at `intern_start!` because background jobs consume broker/waitable capacity; blocking `intern_run!` remains permissive until examples and result validation are stronger. Result validation is exposed as an explicit helper for coordinator trust decisions rather than automatically mutating sync envelopes.
- Remaining: Low-confidence result rejection, richer schema/shape checks beyond `result.ok` + `result.evidence`, and deciding whether `intern_run!` should opt into strict contracts by default later.
- Next: Add confidence/evidence-threshold result policy and wire optional result validation into `intern_sync!`/coordinator-side trust decisions.

### 2026-05-18
- Did: Added first Edict-level contract validators in `intern.edict` and regression coverage in `InternWorkerTest.InternContractValidatorsCheckTaskAndStatusShape`.
- Decided: Keep the first validators opt-in/helper-level rather than enforcing them in `intern_run!` / `intern_start!` immediately, so the public module-backed worker surface remains compatible while contract shape is refined.
- Remaining: Safe task-class examples, result validation beyond public envelope shape, and low-confidence/malformed-result rejection.
- Next: Add gather/classify/filter contract examples and first result-validation helper for expected result shape/success evidence.

## Acceptance Criteria
- [x] Intern tasks have explicit, machine-checkable success criteria in the first helper layer (`expect.success_field`).
- [x] At least gather/classify/filter task contracts are represented.
- [ ] Coordinator validation can reject malformed or low-confidence results beyond first task/envelope shape checks; malformed/over-broad async tasks are rejected before dispatch and the first explicit result validator rejects missing success/evidence fields.
- [ ] Documentation states the “intern, not architect” constraint clearly.
