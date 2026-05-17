# Goal: G091 — Intern Worker Concurrency MVP

**Status**: ACTIVE

**Created**: 2026-05-14  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective
Implement the first practical local-intern worker architecture: a coordinator EdictVM dispatches a bounded task to a worker with a fresh private slab/VM, read-only access to shared context/imports, and a structured result copied back into coordinator-owned state.

## Source Extracted From
Conversation summary section **“The Intern Architecture”** and **“Suggested first implementation step”**.

## Rationale
The intern model is the clearest application-level payoff for AgentC's substrate: small/local models can gather, classify, filter, compress, or proxy context while the primary agent spends tokens deciding. The architecture should preserve Listree safety by avoiding cross-thread mutation of coordinator-owned state.

## Candidate Task Classes
- Information gathering: read files and extract bounded facts.
- Output classification: run tests and classify failures.
- Search + filter: find candidates and discard irrelevant matches.
- Compression: summarize diffs/logs into semantic findings.
- Context proxy: hold a large context and answer factual questions.

## Implementation Plan
- [x] Define a worker task envelope and result schema suitable for Edict and C++ tests.
- [x] Let the coordinator mark FFI imports and task context as recursive `ReadOnly` before dispatch.
- [x] Spawn a worker with a fresh EdictVM, read-only references or safe snapshots of shared context/imports, and private mutable workspace.
- [x] Execute a deterministic Edict worker task first; defer live local-model execution until the substrate is stable.
- [x] Return structured results through a thread-safe join result compatible with the existing StreamManager pattern.
- [x] Copy/merge the result Listree into the coordinator slab on the coordinator/main thread.
- [x] Add async intern jobs as the primary next path, shaped by the G110 Root1 resource-broker/waitable model; LLM streaming/ghost-queue mechanics remain a fallback/reference for the in-process queue.
- [x] Add `intern_start!` / `intern_sync!` while keeping `intern_run!` as blocking convenience over the same worker helper.
- [x] Add broker-compatible async cancellation/backpressure policy and regression coverage.
- [ ] Harden explicit private slab/arena cleanup for the later multi-worker scheduler; the first slice uses normal `CPtr`/VM lifetime cleanup and JSON result copying.

## Acceptance Criteria
- [x] A checked-in test demonstrates coordinator dispatch to at least one worker VM and structured result collection.
- [x] Worker mutation of shared read-only context is refused or isolated.
- [x] Coordinator-owned Listree state is mutated only on the coordinator/main thread.
- [x] Worker output is copied into the main slab with deterministic validation.
- [x] Documentation captures the safe intern-task rule: bounded task, explicit inputs, clear success criteria.

## Current Status — 2026-05-16
G091 is active. The first deterministic substrate slice is implemented as the `intern_run!` builtin, and the broker-compatible async slice now includes `intern_start!` / `intern_sync!` / `intern_cancel!`:

- The coordinator pushes a task envelope with `task_id`/`id`, required `program`, optional `input`, `context`, and `imports`.
- `intern_run!` recursively freezes `context` and `imports`, snapshots `input` through JSON, launches a fresh worker `EdictVM` on a native thread with private `workspace`, joins the worker, and returns a structured result envelope.
- Worker output is serialized to JSON in the worker and parsed back into a coordinator-owned `ListreeValue` after join, so coordinator state is merged only on the coordinator thread.
- `edict/tests/intern_worker_test.cpp` proves deterministic worker execution, structured result collection, refused assignment/removal mutation of shared read-only context, private input snapshot behavior, async cancellation/backpressure envelopes, and structured invalid-envelope errors.
- Documentation now captures the safe intern-task rule in the README, Edict language reference, LLM guide, and 🔗[K032 — Edict Intern Worker Surface](../../Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md).

First async slice details:

- `intern_start!` parses the same task envelope, freezes/snapshots the boundary, creates a broker-compatible job id/waitable, launches a detached worker thread, and returns a handle with `state: "started"` plus a `waitable` object.
- The current backend uses an Edict-local `InternJobManager` and the G110 `Root1ResourceBroker` descriptor model. Worker completion publishes a `Complete`/`Error` mailbox descriptor and reserves `publication: null` for future slab results.
- `intern_sync!` accepts a job id string or handle object, drains broker descriptors on the coordinator thread, returns `state: "running"` or `state: "cancel_requested"` while incomplete, and returns the final structured result copied through JSON once ready.
- `intern_cancel!` is now hoisted into a plain bootstrap Edict word over `intern_sync!` rather than a dedicated VM opcode; it requests cooperative cancellation, emits a `Cancelled` descriptor, and causes final sync to return `state: "cancelled"` without merging the worker result. It does not preemptively kill the current detached thread.
- `intern_start!` supports task `max_active_jobs`; when the active-job count is at the limit it returns `state: "backpressure"`, `error.code: "backpressure"`, and a `Backpressure` descriptor without launching a worker.
- `intern_run!` remains blocking convenience over the same deterministic worker helper.

Remaining G091 hardening before closure: decide whether explicit worker arena/slab lifecycle cleanup is required and execute/coordinate 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](../G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md), which is moving the remaining intern-specific opcode behavior behind importable Root1/worker primitives and plain Edict words. The first G111 slices now provide module-backed intern words and split module-backed `intern_sync!` into lower-level drain/collect composition; deeper task-prep/backpressure/cancellation/envelope policy still needs to move into Edict. Then promote 🔗[G099 — Intern Task Quality Contracts](../G099-InternTaskQualityContracts/index.md) with the now-concrete event/backpressure/cancel fields.

## Implemented Path — Async Intern Jobs
The G091 async slice is shaped by 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md). The earlier ghost-queue mechanics remain a useful implementation reference and fallback, but the implemented abstraction is a broker-compatible waitable job/mailbox:

1. Main/coordinator thread creates an intern job id and freezes/snapshots the task boundary.
2. A job record is represented as a logical waitable/mailbox descriptor compatible with Root1 resource-broker semantics.
3. A background worker thread owns blocking Edict worker execution for the first implementation; later process workers use the same logical descriptor shape.
4. Worker publishes events/final JSON or future publication handles into a broker-compatible queue/result slot.
5. Main/coordinator thread continues other Edict work and periodically calls `intern_sync!`.
6. `intern_sync!` drains pending events and, once complete, parses final JSON into coordinator-owned Listree state on the coordinator thread.
7. Only the coordinator thread mutates coordinator-owned VM/Listree state.

Edict surface:

```edict
task intern_start! @job
-- coordinator may keep working here
job.job_id intern_sync! @status
job.job_id intern_cancel! @cancel_status
```

Implementation choice:
- Added an Edict-local `InternJobManager` modeled on the intended G110 broker shape instead of coupling raw `libedict` to the full cpp-agent runtime/provider stack.
- Job handles expose broker-compatible `waitable.kind = "root1-broker"`, job ids, worker identity, events, cancel/backpressure states, and reserved `publication` fields.
- Later cleanup may still extract shared queue/result mechanics, but only if it preserves the Root1 broker abstraction.

`intern_run!` should remain as blocking sugar over the same worker helper so existing tests and script examples continue to work.

Result envelopes should reserve a `publication` field for future read-only slab publication even while the first async slice returns JSON-copied results. This keeps the control-plane API compatible with later Root1 slab-advertisement work.

Safety hardening discovered after the first slice is now addressed for public VM/Cursor paths: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md) found that recursively frozen dictionaries could be mutated by removal paths through `Cursor::remove()` / `ListreeItem::getValue(pop=true)`, and 🔗[G109 — Listree ReadOnly Mutation Surface Hardening](../G109-ListreeReadOnlyMutationSurfaceHardening/index.md) fixed assignment/removal/list-pop/cleanup-pruning guards before broader async-worker trust.

Related architecture concept: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md) records the longer-term memory-substrate direction: static read-only core/import slabs, private per-VM overlays, Root1-brokered mutable coordination/mailbox slabs, optional published communication slabs, and process-isolated micro-VM cores for near-zero-copy intern scaling. 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md) has defined the logical waitable/mailbox shape used by the first async intern backend. 🔗[G111](../G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) now captures the migration path for replacing intern-specific VM opcodes with importable native primitives plus Edict module policy. Do not jump directly to a full allocator rewrite from G091; keep hardening broker-compatible async worker semantics first, then let G103–G110 harden the slab substrate in staged slices.

## Validation — 2026-05-16
- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*'` — passed 8/8 including async start/sync, cancellation, backpressure, unknown-job coverage, shared-context assignment/removal refusal, and the first G111 module-backed FFI worker-primitive regression.
- Raw CLI smoke with a multi-line task envelope and `intern_run! to_json! print` returned an `ok` envelope with result `{"observed":"alpha"}`.
- Focused Edict regression slice including `FreezeBuiltin.*`, `InternWorkerTest.*`, `EdictVM.*`, `VMStackTest.*`, `SimpleAssignTest.*`, `RegressionMatrixTest.*`, and `PiSimulationTest.MiniKanrenLogicExample` passed 54/54 after adding the first G111 module-backed intern primitive coverage.
- `cmake --build build --target cpp_agent_tests -j2` — passed.
- Session persistence guard checks after adding `intern_run` as a volatile startup builtin passed: `EdictSessionCliTest.*` 2/2 and `SessionStateStoreTest.*` 14/14.

## Constraints
- Whole-subtree `ReadOnly` is sufficient for the first implementation.
- Do not share stateful provider handles across workers in the MVP.
- Keep local models in the “intern, not architect” role: extraction/summarization/classification before deep reasoning.
- Preserve current Edict style: adjacent eval (`word!`) and discard as `/` or `/ /`.
