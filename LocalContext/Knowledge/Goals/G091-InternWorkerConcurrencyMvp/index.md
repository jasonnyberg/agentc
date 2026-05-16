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
- [ ] Add async intern jobs as the primary next path, shaped by the G110 Root1 resource-broker/waitable model; LLM streaming/ghost-queue mechanics remain a fallback/reference for the in-process queue.
- [ ] Add `intern_start!` / `intern_sync!` while keeping `intern_run!` as blocking convenience over the same worker helper.
- [ ] Harden explicit private slab/arena cleanup for the later multi-worker scheduler; the first slice uses normal `CPtr`/VM lifetime cleanup and JSON result copying.

## Acceptance Criteria
- [x] A checked-in test demonstrates coordinator dispatch to at least one worker VM and structured result collection.
- [x] Worker mutation of shared read-only context is refused or isolated.
- [x] Coordinator-owned Listree state is mutated only on the coordinator/main thread.
- [x] Worker output is copied into the main slab with deterministic validation.
- [x] Documentation captures the safe intern-task rule: bounded task, explicit inputs, clear success criteria.

## Current Status — 2026-05-14
G091 is active. The first deterministic substrate slice is implemented as the `intern_run!` builtin:

- The coordinator pushes a task envelope with `task_id`/`id`, required `program`, optional `input`, `context`, and `imports`.
- `intern_run!` recursively freezes `context` and `imports`, snapshots `input` through JSON, launches a fresh worker `EdictVM` on a native thread with private `workspace`, joins the worker, and returns a structured result envelope.
- Worker output is serialized to JSON in the worker and parsed back into a coordinator-owned `ListreeValue` after join, so coordinator state is merged only on the coordinator thread.
- `edict/tests/intern_worker_test.cpp` proves deterministic worker execution, structured result collection, refused mutation of shared read-only context, private input snapshot behavior, and structured invalid-envelope errors.
- Documentation now captures the safe intern-task rule in the README, Edict language reference, LLM guide, and 🔗[K032 — Edict Intern Worker Surface](../../Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md).

Remaining G091 hardening before closure: implement the async polling path below, then decide whether explicit worker arena/slab lifecycle cleanup is required before promoting 🔗[G099 — Intern Task Quality Contracts](../G099-InternTaskQualityContracts/index.md).

## Primary Implementation Path — Async Intern Jobs
The next G091 slice should now be shaped by 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md). The earlier ghost-queue mechanics remain a useful implementation reference and fallback, but the target abstraction is a broker-compatible waitable job/mailbox:

1. Main/coordinator thread creates an intern job id and freezes/snapshots the task boundary.
2. A job record is represented as a logical waitable/mailbox descriptor compatible with Root1 resource-broker semantics.
3. A background worker thread owns blocking Edict worker execution for the first implementation; later process workers use the same logical descriptor shape.
4. Worker publishes events/final JSON or future publication handles into a broker-compatible queue/result slot.
5. Main/coordinator thread continues other Edict work and periodically calls `intern_sync!`.
6. `intern_sync!` drains pending events and, once complete, parses final JSON into coordinator-owned Listree state on the coordinator thread.
7. Only the coordinator thread mutates coordinator-owned VM/Listree state.

Planned Edict surface:

```edict
task intern_start! @job
-- coordinator may keep working here
job.job_id intern_sync! @status
```

Preferred implementation options, in order:
- Use G110 to define the logical job waitable/result-envelope shape before committing to a backend.
- If quick implementation is needed, add an Edict-local `InternJobManager` modeled on `StreamManager` but with broker-compatible job ids, event kinds, backpressure/cancel states, and future publication handles.
- Extract generic queue/result mechanics from `cpp-agent/runtime/core/StreamManager` into a neutral shared component only if it does not hide the eventual Root1 broker shape.
- Avoid coupling raw `libedict` directly to the full `cpp-agent` runtime/provider stack.

`intern_run!` should remain as blocking sugar over the same worker helper so existing tests and script examples continue to work.

Result envelopes should reserve a `publication` field for future read-only slab publication even while the first async slice returns JSON-copied results. This keeps the control-plane API compatible with later Root1 slab-advertisement work.

Safety hardening discovered after the first slice: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md) found that recursively frozen dictionaries can currently be mutated by removal paths through `Cursor::remove()` / `ListreeItem::getValue(pop=true)`. 🔗[G109 — Listree ReadOnly Mutation Surface Hardening](../G109-ListreeReadOnlyMutationSurfaceHardening/index.md) should be completed before trusting async workers with shared read-only context.

Related architecture concept: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md) records the longer-term memory-substrate direction: static read-only core/import slabs, private per-VM overlays, Root1-brokered mutable coordination/mailbox slabs, optional published communication slabs, and process-isolated micro-VM cores for near-zero-copy intern scaling. 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md) now bubbles ahead of the async backend decision because it defines the logical waitable/mailbox shape. Do not jump directly to a full allocator rewrite from G091; build broker-compatible async worker semantics first, then let G103–G110 harden the slab substrate in staged slices.

## Validation — 2026-05-14
- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*'` — passed 2/2.
- Raw CLI smoke with a multi-line task envelope and `intern_run! to_json! print` returned an `ok` envelope with result `{"observed":"alpha"}`.
- Focused Edict regression slice including `InternWorkerTest.*`, `EdictVM.*`, `VMStackTest.*`, `SimpleAssignTest.*`, `RegressionMatrixTest.*`, and `PiSimulationTest.MiniKanrenLogicExample` passed 42/42.
- `cmake --build build --target cpp_agent_tests -j2` — passed.
- Session persistence guard checks after adding `intern_run` as a volatile startup builtin passed: `EdictSessionCliTest.*` 2/2 and `SessionStateStoreTest.*` 14/14.

## Constraints
- Whole-subtree `ReadOnly` is sufficient for the first implementation.
- Do not share stateful provider handles across workers in the MVP.
- Keep local models in the “intern, not architect” role: extraction/summarization/classification before deep reasoning.
- Preserve current Edict style: adjacent eval (`word!`) and discard as `/` or `/ /`.
