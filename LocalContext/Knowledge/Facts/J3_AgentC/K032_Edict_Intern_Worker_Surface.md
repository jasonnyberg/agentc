# Knowledge: Edict Intern Worker Surface

**ID**: LOCAL:K032
**Category**: 🔗[J3 AgentC](index.md)
**Tags**: #j3, #edict, #workers, #concurrency, #interns
**Status**: Active
**Last Referenced**: 2026-05-14

## Overview
🔗[G091 — Intern Worker Concurrency MVP](../../Goals/G091-InternWorkerConcurrencyMvp/index.md) landed the first deterministic intern-worker substrate in raw Edict: `intern_run!`.

The purpose is to prove the safe coordinator/worker boundary before introducing live local-model interns or a larger scheduler.

## Coordinator Contract
The coordinator VM owns mutable root state. It dispatches a bounded worker task by pushing a task envelope and evaluating `intern_run!`:

```edict
worker_task intern_run! @worker_result
```

`intern_run!` returns a structured envelope; the coordinator decides whether and how to merge `worker_result.result` into its own state.

## Task Envelope
| Field | Required | Meaning |
|-------|----------|---------|
| `task_id` / `id` | optional | Stable label copied into the result. |
| `program` | required | Bounded Edict source executed inside the worker VM. |
| `input` | optional | Private worker input copied through JSON before dispatch. |
| `context` | optional | Shared context subtree recursively frozen before dispatch. |
| `imports` | optional | Shared import namespace recursively frozen before dispatch. |

## Worker Root
The worker runs in a fresh `EdictVM` with these root fields:

| Field | Meaning |
|-------|---------|
| `task_id` | Copied task id. |
| `input` | JSON snapshot; safe for worker-private reads/mutations. |
| `context` | Read-only shared subtree. |
| `imports` | Read-only shared import namespace. |
| `workspace` | Private mutable scratch object. |
| `result` | Program-assigned structured result; if absent, stack top is used. |

## Result Envelope
| Field | Meaning |
|-------|---------|
| `ok` | `["ok"]` on success, `[]` on failure. |
| `task_id` | Task id. |
| `state` | `complete` or `error`. |
| `worker` | Current backend name, `edict-thread`. |
| `result` | Worker result copied back through JSON on the coordinator thread. |
| `error` | Error object or null. |
| `safety` | Metadata: read-only context/imports, JSON input snapshot, coordinator-thread merge. |

## Safety Rules
- Keep intern tasks bounded, explicit, and checkable.
- Pass only explicit `input`, `context`, and optional `imports`.
- Shared `context`/`imports` are recursively `ReadOnly` before worker launch.
- Worker mutation of shared context is refused by Listree read-only guards.
- Worker input is a JSON snapshot, so worker mutation does not affect coordinator-owned input.
- Worker output is serialized to JSON in the worker and parsed into a fresh coordinator-owned `ListreeValue` after join.
- Do not pass stateful provider handles through context/imports in the MVP.

## Validated Coverage
`edict/tests/intern_worker_test.cpp` covers:
- coordinator dispatch through `intern_run!`
- fresh worker VM execution of a deterministic Edict program
- private `workspace` use
- shared `context` recursive freeze
- refused worker mutation of shared context
- structured result collection into coordinator-owned root state
- structured error result when the task envelope is invalid

Latest focused validation: `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*'` passed 2/2 on 2026-05-14.

## Current Limits
- `intern_run!` is synchronous spawn-and-join; it is not yet a multi-worker scheduler.
- Live local-model intern execution remains out of scope.
- Re-entrancy metadata for imported native functions is future 🔗[G092 — Cartographer FFI Re-entrancy Metadata](../../Goals/G092-CartographerFfiReentrancyMetadata/index.md).
- Finer-grained reference-scoped read-only sharing remains deferred in 🔗[G093 — Reference-Scoped ReadOnly Sharing](../../Goals/G093-ReferenceScopedReadOnlySharing/index.md).
