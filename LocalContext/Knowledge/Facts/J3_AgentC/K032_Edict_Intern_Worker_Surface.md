# Knowledge: Edict Intern Worker Surface

**ID**: LOCAL:K032
**Category**: 🔗[J3 AgentC](index.md)
**Tags**: #j3, #edict, #workers, #concurrency, #interns
**Status**: Active
**Last Referenced**: 2026-05-17

## Overview
🔗[G091 — Intern Worker Concurrency MVP](../../Goals/G091-InternWorkerConcurrencyMvp/index.md) landed the first deterministic intern-worker substrate in raw Edict: `intern_run!`. It now also has the first broker-compatible async surface: `intern_start!` / `intern_sync!` / `intern_cancel!`.

The purpose is to prove the safe coordinator/worker boundary before introducing live local-model interns or a larger scheduler. 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](../../Goals/G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) now tracks the migration to expose only irreducibly native Root1/worker mechanisms through importable libraries and move intern policy/envelopes into Edict modules. The first G111 slices added importable LTV worker primitives plus `worker.edict` / `intern.edict`; module-backed `intern_sync!` now composes lower-level drain/collect words in Edict, `worker.edict_active_count!` exposes job-table pressure, and VM opcodes remain as compatibility shims for now.

## Coordinator Contract
The coordinator VM owns mutable root state. It can dispatch a bounded worker task synchronously by pushing a task envelope and evaluating `intern_run!`:

```edict
worker_task intern_run! @worker_result
```

`intern_run!` returns a structured envelope; the coordinator decides whether and how to merge `worker_result.result` into its own state. For async dispatch, use:

```edict
worker_task intern_start! @job
job.job_id intern_sync! @status
job.job_id intern_cancel! @cancel_status
```

`intern_start!` returns a job handle with `job_id`, `state: "started"`, a broker-shaped `waitable`, and reserved `publication: null`. `intern_sync!` returns `state: "running"` or `state: "cancel_requested"` until the worker completes, then returns the same structured final result shape as `intern_run!` plus `job_id`, `waitable`, and broker descriptor `events`. `intern_cancel!` is a plain bootstrap Edict word over `intern_sync!`; it requests cooperative cancellation and final sync reports `state: "cancelled"` without merging the worker result.

## Task Envelope
| Field | Required | Meaning |
|-------|----------|---------|
| `task_id` / `id` | optional | Stable label copied into the result. |
| `program` | required | Bounded Edict source executed inside the worker VM. |
| `input` | optional | Private worker input copied through JSON before dispatch. |
| `context` | optional | Shared context subtree recursively frozen before dispatch. |
| `imports` | optional | Shared import namespace recursively frozen before dispatch. |
| `max_active_jobs` | optional | Async backpressure limit. If active jobs are already at this limit, `intern_start!` returns `state: "backpressure"` and does not launch a worker. |

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
- Worker assignment and removal attempts against shared context are refused by Listree read-only guards.
- Worker input is a JSON snapshot, so worker mutation does not affect coordinator-owned input.
- Worker output is serialized to JSON in the worker and parsed into a fresh coordinator-owned `ListreeValue` after join.
- Do not pass stateful provider handles through context/imports in the MVP.

## Validated Coverage
`edict/tests/intern_worker_test.cpp` covers:
- coordinator dispatch through `intern_run!`
- fresh worker VM execution of a deterministic Edict program
- private `workspace` use
- shared `context` recursive freeze
- refused worker assignment/removal mutation of shared context
- structured result collection into coordinator-owned root state
- structured error result when the task envelope is invalid

Latest focused validation: `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*'` passed 8/8 on 2026-05-17, including async `intern_start!` / `intern_sync!`, `intern_cancel!`, backpressure, unknown-job coverage, the first module-backed FFI worker-primitive coverage, direct lower-level worker drain/collect coverage, and active-count coverage.

## Async Path
The async path is broker-compatible with 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../../Goals/G110-EventfdEpollMicroVmIpcDesign/index.md). The LLM streaming/ghost-queue mechanics remain useful as an in-process reference, but the durable abstraction is now a logical job waitable/mailbox:

```edict
task intern_start! @job
-- coordinator continues other work
job.job_id intern_sync! @status
job.job_id intern_cancel! @cancel_status
```

Implemented behavior:
- `intern_start!` freezes/snapshots the task boundary, creates a job id/waitable descriptor, launches a detached/background worker, and returns immediately.
- The background worker owns blocking Edict worker execution and publishes final `Complete`/`Error` descriptors through a G110 `Root1ResourceBroker` participant mailbox.
- `intern_sync!` runs on the coordinator thread, drains events, observes completion/error/cancellation, parses final JSON into coordinator-owned Listree state only for non-cancelled success, removes completed jobs, and returns a final structured envelope.
- `intern_cancel!` is implemented as a plain bootstrap Edict word that pushes the `'cancel` control marker and delegates to `intern_sync!`; it marks an active job as cooperatively cancelled, emits a `Cancelled` descriptor, and causes final sync to return `state: "cancelled"`; this first slice does not preemptively kill the detached worker thread.
- `intern_start!` reports `Backpressure` when `max_active_jobs` is exceeded and does not launch a worker.
- `intern_run!` remains blocking convenience over the same worker helper.

Implementation approach:
1. Uses an Edict-local `InternJobManager` rather than coupling raw `libedict` to the full `cpp-agent` runtime/provider stack.
2. Job handles and final envelopes expose broker-compatible `waitable.kind = "root1-broker"`, `job_id`, `events`, cancellation/backpressure states, and reserved `publication` fields.
3. G110's mapped coordination slabs and pidfd owner-death descriptors remain available for later process-isolated workers.

Longer-term memory substrate: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md) records the proposed static read-only core/import slabs, private per-VM overlays, Root1-brokered mutable coordination/mailbox slabs, optional published communication slabs, eventfd/epoll waitables, and process-isolated micro-VM cores.

## Current Limits
- `intern_start!` / `intern_sync!` / `intern_cancel!` are implemented, and G111 now provides a module-backed path using imported worker primitives; module-backed `intern_sync!` composes separate drain/collect words, but `intern_run!`, `intern_start!`, and `intern_sync!` are still registered as VM opcode compatibility shims until task prep/backpressure/cancellation/envelope policy is split further and tests/docs migrate fully.
- Cancellation is cooperative-only; explicit worker arena lifecycle cleanup and process-isolated workers remain future work.
- 🔗[G109 — Listree ReadOnly Mutation Surface Hardening](../../Goals/G109-ListreeReadOnlyMutationSurfaceHardening/index.md) is complete for public VM/Cursor paths: recursive shared-context freeze now blocks assignment, path removal, remove-head, list pop, and cleanup-pruning mutation attempts.
- Live local-model intern execution remains out of scope.
- Re-entrancy metadata for imported native functions is future 🔗[G092 — Cartographer FFI Re-entrancy Metadata](../../Goals/G092-CartographerFfiReentrancyMetadata/index.md).
- Finer-grained reference-scoped read-only sharing remains deferred in 🔗[G093 — Reference-Scoped ReadOnly Sharing](../../Goals/G093-ReferenceScopedReadOnlySharing/index.md).
