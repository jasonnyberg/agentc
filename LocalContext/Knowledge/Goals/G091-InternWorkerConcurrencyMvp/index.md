# Goal: G091 — Intern Worker Concurrency MVP

**Status**: ACTIVE / THREAD-BACKEND PHASE BOUNDED

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
- [x] Harden first lifecycle/drop semantics for the current thread-worker backend: active counts now exclude terminal retained jobs, final async statuses are retained for repeated sync until explicit drop or bounded retention sweep, running drops explicitly abandon the handle without killing the detached worker, and cancellation is non-retroactive once a worker is terminal.
- [x] Add first worker-visible cooperative cancellation checkpoint protocol: async worker programs that execute `yield!` are resumed by the worker runtime only after checking the job cancellation token, allowing long-running cooperative workers to terminate before executing later program steps.
- [x] Document the private arena/slab cleanup boundary for the current thread-worker backend versus future G103/G105/G107 process/slab workers; current cleanup relies on scoped VM/CPtr teardown, JSON result copying, explicit job drop/terminal retention policy, and lifecycle accounting rather than premature slab ownership machinery.
- [ ] Harden explicit private slab/arena cleanup when the later multi-worker/process scheduler introduces separate worker arenas, static slab mounts, or published result slabs.

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

G111 is complete, so remaining G091 hardening is now lifecycle/resource policy over the module-backed worker surface: deeper private arena/slab cleanup for the later scheduler and clearer abandoned detached-worker accounting. First lifecycle cleanup landed on 2026-05-18: terminal async jobs are retained for repeated `intern_sync!` until `worker.edict_drop!` or bounded terminal-retention sweep, `worker.edict_active_count!` counts only non-terminal/non-abandoned jobs, dropping a running job returns `state: "abandoned"` and suppresses later mailbox publication, dropping a terminal retained job returns `state: "dropped"`, and cancellation is non-retroactive after terminal completion. First worker-visible cooperative cancellation checkpointing landed on 2026-05-19: async worker programs that reach `yield!` hand control to the worker runtime, which checks the job cancellation token before resuming the code frame. First abandoned-worker accounting also landed on 2026-05-19: `worker.edict_lifecycle_status!` exposes active/tracked/retained-terminal/abandoned-running counts plus started/finished/abandoned/dropped/swept cumulative counters.

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
- Lifecycle policy after the first cleanup slice:
  - active count means jobs whose worker has not reached terminal state and whose handle has not been abandoned;
  - terminal jobs are retained so repeated `intern_sync!` returns the same final envelope until explicit `worker.edict_drop!` or bounded terminal-retention sweep;
  - `worker.edict_drop!` on a terminal job forgets the retained result and returns `state: "dropped"`;
  - `worker.edict_drop!` on a running job forgets the handle, marks the job `abandoned`, returns `state: "abandoned"`, and lets the detached thread unwind without publishing a now-orphaned completion descriptor;
  - `worker.edict_lifecycle_status!` reports lifecycle/resource accounting for the current process-local manager: active/tracked/retained-terminal/abandoned-running counts and cumulative started/finished/abandoned/terminal-dropped/terminal-swept counters;
  - `intern_cancel!` / `worker.edict_request_cancel!` is cooperative and non-retroactive: if the worker is still running, final collection reports `cancelled`; if the worker is already terminal, cancellation is a no-op and collection returns the terminal complete/error result.
- Later cleanup may still extract shared queue/result mechanics, but only if it preserves the Root1 broker abstraction.

`intern_run!` should remain as blocking sugar over the same worker helper so existing tests and script examples continue to work.

Result envelopes should reserve a `publication` field for future read-only slab publication even while the first async slice returns JSON-copied results. This keeps the control-plane API compatible with later Root1 slab-advertisement work.

## Private Arena / Slab Cleanup Boundary — 2026-05-19

The current G091 backend is a **thread-worker control-plane proof**, not the final slab-isolated worker substrate. Its cleanup contract is intentionally narrower than future G103/G105/G107 work:

### Current thread-worker backend owns

- **Fresh worker VM lifetime**: each blocking or async worker constructs a fresh `EdictVM` and worker root. VM stacks, code frames, transient execution state, private `workspace`, and worker-local result construction are normal process heap/`CPtr` state that are released when `runInternWorker` returns and the thread unwinds.
- **Coordinator-safe transfer**: `input` is JSON-snapshotted before dispatch; `context` and `imports` are recursively marked `ReadOnly`; worker result JSON is parsed into coordinator-owned `ListreeValue` state only on coordinator/main-thread collection.
- **Async handle lifecycle**: `InternJobManager` owns process-local job records, Root1-compatible participant mailboxes, cancellation tokens, retained terminal statuses, explicit drop semantics, and bounded terminal retention. `worker.edict_lifecycle_status!` exposes active/tracked/retained-terminal/abandoned-running counts and cumulative started/finished/abandoned/dropped/swept counters.
- **Abandoned running jobs**: running `drop` is handle abandonment, not thread preemption. The detached worker continues to unwind naturally, suppresses orphan completion publication, and decrements abandoned-running accounting when it exits.
- **Intentional process-lifetime manager**: the manager remains intentionally process-lifetime/leaked because detached workers may still reference broker/job resources during shutdown; letting the OS reclaim process resources is safer than destructing broker state while detached workers can still finish.

### Current thread-worker backend explicitly does not own

- separate mmap worker arenas or slab id ranges;
- durable private overlay slabs;
- immutable static core/import slab mounting;
- OS-level read-only mappings or no-mutate retain/release semantics;
- worker-published immutable result slabs;
- cross-process pidfd/forkserver launch or process-local native sidecar rehydration;
- durable reconstruction of async job records after process crash.

### Future handoff boundaries

- 🔗[G103 — Build-Time Static Core Declaration Image MVP](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md) owns declarative static import/module images and manifest validation. G091 should not serialize process-local worker VM frames or native handles into those static slabs.
- 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md) owns no-mutate retain/release, immortal/static slab identification, and cursor/pin behavior for OS-read-only mapped nodes. G091 should continue using logical `ReadOnly` plus JSON copying until this ownership model exists.
- 🔗[G106 — Root1 Slab Advertisement Registry](../G106-Root1SlabAdvertisementRegistry/index.md) owns advertised immutable publication roots, slab ranges, epochs, and lease/permission metadata. G091's `publication: null` field is only the API reservation for that future path.
- 🔗[G107 — Process-Isolated Micro-VM Interns](../G107-ProcessIsolatedMicroVmInterns/index.md) owns process launch, static image mounting, private dynamic overlays, process-local capability rehydration policy, pidfd owner-death handling, and returning results/publications across a Root1 mailbox boundary.

### Reopen G091 implementation when

- worker jobs acquire explicit arena/slab handles that need release separate from C++ scope teardown;
- results are published as immutable slab roots instead of JSON-copied values;
- async job records must survive process restart/session resume;
- process-isolated workers require coordinator-visible resource accounting beyond the current thread-worker lifecycle counters.

Until one of those conditions is true, additional G091 arena cleanup code would be premature and risks duplicating G103/G105/G106/G107 responsibilities.

Safety hardening discovered after the first slice is now addressed for public VM/Cursor paths: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md) found that recursively frozen dictionaries could be mutated by removal paths through `Cursor::remove()` / `ListreeItem::getValue(pop=true)`, and 🔗[G109 — Listree ReadOnly Mutation Surface Hardening](../G109-ListreeReadOnlyMutationSurfaceHardening/index.md) fixed assignment/removal/list-pop/cleanup-pruning guards before broader async-worker trust.

Related architecture concept: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md) records the longer-term memory-substrate direction: static read-only core/import slabs, private per-VM overlays, Root1-brokered mutable coordination/mailbox slabs, optional published communication slabs, and process-isolated micro-VM cores for near-zero-copy intern scaling. 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md) has defined the logical waitable/mailbox shape used by the first async intern backend. 🔗[G111](../G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) now captures the migration path for replacing intern-specific VM opcodes with importable native primitives plus Edict module policy. Do not jump directly to a full allocator rewrite from G091; keep hardening broker-compatible async worker semantics first, then let G103–G110 harden the slab substrate in staged slices.

## Progress Notes

### 2026-05-19
- Did: Added first abandoned-worker accounting over the module-backed worker primitive surface. `worker.edict_lifecycle_status!` now reports current active/tracked/retained-terminal/abandoned-running counts plus cumulative started/finished/abandoned/terminal-dropped/terminal-swept counters. The detached worker exit path decrements abandoned-running accounting without publishing orphan completion descriptors.
- Did: Added the first worker-visible cooperative cancellation checkpoint protocol. Async worker jobs now carry a shared cancellation token into `runInternWorker`; when worker code executes `yield!`, the worker runtime checks the token before calling `EdictVM::resume()`. If cancellation has been requested, the worker stores a cancelled outcome immediately and does not execute later program steps.
- Decided: `yield!` is the first cancellation checkpoint boundary because it already records/resumes VM frame state and avoids new intern-specific VM opcodes. This keeps cancellation cooperative and explicit in worker programs. Running `drop` remains handle abandonment rather than preemption, but now has visible lifecycle accounting.
- Did: Documented the private arena/slab cleanup boundary for the current thread-worker backend versus future G103/G105/G106/G107 static-slab, publication, and process-isolated worker responsibilities.
- Decided: Current G091 thread-worker cleanup is sufficient until explicit worker arena/slab handles, immutable result publications, durable async job records, or process-isolated workers exist. More slab cleanup code now would duplicate planned G103/G105/G106/G107 ownership work.
- Remaining: Richer resource accounting and explicit cleanup only when process-isolated workers/slab publications exist.
- Next: Pivot to the next substrate prerequisite rather than adding premature G091 slab code; likely G103 static declaration image MVP or G105 static slab ownership audit, depending on desired sequencing.

### 2026-05-18
- Did: Landed first lifecycle/drop semantics on the completed G111 module-backed worker surface: terminal async statuses are retained for repeated sync until explicit drop or bounded sweep, active counts exclude terminal/abandoned jobs, running drops return `abandoned`, terminal drops return `dropped`, abandoned workers suppress orphan completion publication, and cancellation is non-retroactive after terminal completion.
- Decided: Running `drop` is handle abandonment rather than thread preemption; current detached workers must unwind naturally until a later worker-visible cancellation/checkpoint mechanism exists.
- Remaining: Deeper private arena/slab cleanup for the later scheduler, abandoned-worker/resource accounting beyond handle abandonment, and worker-visible cancellation checkpoints.
- Next: Add a worker-visible cancellation checkpoint primitive or protocol so long-running Edict workers can voluntarily stop before finishing the full program.

## Validation — 2026-05-19
- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*:Root1AwaitSchedulerTest.*:Root1PrimitiveModuleTest.*:EdictVM.YieldedExecutionCanResumeCurrentCodeFrame' --gtest_brief=1` — passed 21/21 after abandoned-worker accounting; earlier checkpoint slice passed 16/16.
- Earlier focused checkpoint slice: `./build/edict/edict_tests --gtest_filter='InternWorkerTest.WorkerYieldCheckpointsObserveAsyncCancellation:InternWorkerTest.InternCancelRequestsCancellationAndFinalSyncReportsCancelled' --gtest_brief=1` — passed 2/2.

## Validation — 2026-05-18
- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*' --gtest_brief=1` — passed 11/11 including lifecycle retention/drop/abandon/cancellation semantics and first G099 contract validators.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*:Root1PrimitiveModuleTest.*' --gtest_brief=1` — passed 12/12.
- Focused Edict regression slice including `FreezeBuiltin.*`, `InternWorkerTest.*`, `Root1PrimitiveModuleTest.*`, `EdictVM.*`, `VMStackTest.*`, `SimpleAssignTest.*`, `RegressionMatrixTest.*`, and `PiSimulationTest.MiniKanrenLogicExample` passed 58/58.

Earlier validation:
- Raw CLI smoke with a multi-line task envelope and `intern_run! to_json! print` returned an `ok` envelope with result `{"observed":"alpha"}`.
- `cmake --build build --target cpp_agent_tests -j2` — passed.
- Session persistence guard checks after adding `intern_run` as a volatile startup builtin passed: `EdictSessionCliTest.*` 2/2 and `SessionStateStoreTest.*` 14/14.

## Constraints
- Whole-subtree `ReadOnly` is sufficient for the first implementation.
- Do not share stateful provider handles across workers in the MVP.
- Keep local models in the “intern, not architect” role: extraction/summarization/classification before deep reasoning.
- Preserve current Edict style: adjacent eval (`word!`) and discard as `/` or `/ /`.
