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
- [ ] Provide examples for each safe task class.
- [ ] Integrate the contract with the worker dispatch path from 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md).
- [x] Add first tests that reject underspecified intern tasks and validate public status-envelope shape.

## Implementation Progress — 2026-05-18

First contract-validation slice landed in `intern.edict`:

- `intern.validate_task_contract!` accepts a task-like object and returns `state: "contract_valid"` when it has a non-empty `program` and `expect.success_field`; it preserves `task_id`, `limits`, and `expect` in the returned status.
- Missing required fields return `state: "contract_error"` with structured `error.code` / `error.message`; the first checked error is `missing_program`, with `missing_success_field` also represented.
- `intern.validate_status_envelope!` validates that a G111 public status/envelope has a visible `state` and returns `state: "envelope_valid"` with the envelope state, job/task ids, and publication field.
- `InternWorkerTest.InternContractValidatorsCheckTaskAndStatusShape` covers a valid contract, an underspecified contract, and a valid public status envelope.

This is intentionally a first reusable helper layer rather than mandatory dispatch enforcement. A later G099 slice should decide where `intern_start!` / `intern_run!` enforce contracts by default versus where callers opt into contract validation.

## Acceptance Criteria
- [x] Intern tasks have explicit, machine-checkable success criteria in the first helper layer (`expect.success_field`).
- [ ] At least gather/classify/filter task contracts are represented.
- [ ] Coordinator validation can reject malformed or low-confidence results beyond first task/envelope shape checks.
- [ ] Documentation states the “intern, not architect” constraint clearly.
