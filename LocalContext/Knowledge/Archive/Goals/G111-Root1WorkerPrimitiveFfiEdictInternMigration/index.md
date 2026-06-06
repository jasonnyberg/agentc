# Goal: G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration

**Status**: COMPLETE
**Created**: 2026-05-17  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)  
**Depends On / Extends**: 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md), 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)  
**Feeds**: 🔗[G099 — Intern Task Quality Contracts](../G099-InternTaskQualityContracts/index.md), 🔗[G107 — Process-Isolated Micro-VM Interns](../G107-ProcessIsolatedMicroVmInterns/index.md)

## Objective

Migrate the remaining intern-specific VM opcodes (`intern_run!`, `intern_start!`, and `intern_sync!`) to an Edict-first model:

1. native C/C++ FFI libraries expose only the primitives that Edict cannot implement directly;
2. low-level primitive words are importable and namespaced, even if most users never call them directly;
3. higher-level intern behavior and envelopes are implemented as plain Edict words;
4. the VM core stops accumulating domain-specific intern opcodes.

The intended end state is:

```text
VM core
  = generic execution + stack/dictionary/context mechanics + JSON/LTV conversion + FFI/import

imported native primitive libraries
  = eventfd/epoll/pidfd/mmap/atomic/thread/process/fresh-worker-VM operations

Edict modules
  = Root1 primitive wrappers, worker primitive wrappers, intern task policy, user-facing intern words
```

## Rationale

Edict can already import and call C/C++ through Cartographer FFI. That means eventfd/epoll/worker mechanisms do not need to live in the VM opcode enum merely because they require native implementation. The cleaner boundary is:

- **C/C++ where the OS or concurrency substrate is irreducibly native**: fd creation, epoll wait sets, pidfds, mapped atomic resource words, worker thread/process spawning, and worker completion slots.
- **Edict where the behavior is policy or composition**: task validation, defaulting, freezing/snapshotting, cancellation semantics, backpressure policy, result-envelope shape, and public UI words.

This preserves Edict as the control plane and keeps the VM small. It also matches the Root1 architecture: raw fds/eventfds/epoll registrations are transient process-local capabilities, while Edict-visible state should be logical handles, resource keys, participant ids, descriptors, waitables, and publication handles.

It is acceptable, and desirable, to expose low-level primitive words such as `root1.mailbox_drain!` or `worker.edict_collect_status!` even if normal users only call `intern_sync!`. The low-level words make the substrate composable for future provider streams, process-isolated workers, explicit mailboxes, and `await!` without forcing every new async feature into VM dispatch.

## Current State

- `VMOP_INTERN_CANCEL`, `VMOP_INTERN_RUN`, `VMOP_INTERN_START`, and `VMOP_INTERN_SYNC` have been removed from the VM opcode enum, dispatch table, method declarations, method definitions, and bootstrap builtin/capsule registration.
- Raw VM startup no longer installs `intern_run`, `intern_start`, `intern_sync`, or `intern_cancel` as bootstrap builtins. The public intern words now live in `cpp-agent/edict/modules/intern.edict` and are loaded over imported worker primitives.
- The transitional native worker implementation is now split by first-order concern:
  - `edict/edict_worker_runtime.{h,cpp}` owns fresh worker-VM execution and join-slot result transfer;
  - `edict/edict_worker_primitives.cpp` owns task parsing/preparation/status facts, the process-local async job table, G110 `Root1ResourceBroker` descriptor publication/drain, native cancellation mechanics, and hard caps;
  - `edict/edict_worker_primitives_ffi.cpp` owns the thin `agentc_worker_edict_*_ltv` C ABI / LTV handle wrappers.
- `intern.edict` now owns public composition and envelope shaping for run/start/sync/cancel. Remaining native work is explicitly scoped as follow-up lifecycle/resource policy under G091/G110: hard worker safety caps remain native safeguards, and broader job retention/abandoned-worker semantics are no longer part of the opcode-to-FFI/Edict migration.

G111 has separated those layers so the VM core no longer owns intern behavior: irreducible worker/Root1 mechanisms are importable C++ primitives and public intern policy/envelope composition lives in Edict modules.

## Target Library/Module Shape

### Native primitive libraries

The exact library names are not fixed, but the split should be approximately:

```text
libagentc_root1_primitives.so
libagentc_worker_primitives.so
```

or a single first-slice library if that reduces build/test complexity:

```text
libagentc_root1_worker_primitives.so
```

These libraries should be importable from Edict with the existing resolver/Cartographer path, for example:

```edict
[<root1_worker_lib>] [<root1_worker_header>] resolver.import! @root1ffi
```

### Edict modules

Layer Edict words over those primitives:

```text
cpp-agent/edict/modules/root1.edict   -- low-level Root1/broker/waitable/mailbox wrappers
cpp-agent/edict/modules/worker.edict  -- low-level fresh Edict-worker wrappers
cpp-agent/edict/modules/intern.edict  -- public intern task policy and user-facing words
```

The curated launcher can preload these modules later, but the first implementation may use explicit imports in tests.

## Native Primitives That Are Justified

Native primitives should be small and reusable, not intern-specific policy blobs.

### Root1 / event substrate primitives

Candidate first-slice primitives:

- register/reconstruct a Root1 participant;
- poll/await Root1 waitables with a timeout;
- drain a participant mailbox into descriptor objects;
- send a descriptor to a participant mailbox;
- send cancellation/backpressure descriptors;
- try-acquire/release a mapped resource key;
- attach pidfd owner-death reporting where available.

These can expose logical handles and descriptor JSON/LTV objects. They should not expose raw fds as durable Edict state.

### Worker primitives

Candidate first-slice primitives:

- start a fresh Edict worker VM with:
  - program source;
  - JSON-snapshotted input;
  - read-only shared context/imports where safe;
  - destination Root1 participant/mailbox;
- query whether a worker result is ready;
- collect a final worker result as JSON or LTV on the coordinator thread;
- drop/forget a completed worker job record;
- request a native cancellation flag if/when workers learn to check it;
- expose native active-worker counts and hard-cap errors.

The first worker-start primitive should prefer LTV arguments for `context` and `imports` so G091's read-only shared-context behavior is preserved. JSON-only worker start is acceptable as an interim isolation fallback, but it should be called out as a semantic change because it copies context/imports rather than sharing frozen subtrees.

## Edict-Owned Semantics

These should move out of C++ opcode implementation and into plain Edict modules:

- task object validation and default `task_id` handling;
- required `program` checks;
- `input` JSON snapshotting;
- recursive `freeze!` of `context` and `imports`;
- `max_active_jobs` / backpressure policy;
- public envelopes for `started`, `running`, `cancel_requested`, `complete`, `error`, `cancelled`, and `backpressure`;
- `publication: null` reservation and future publication-handle plumbing;
- `intern_cancel!` as a control word over `intern_sync!`;
- `intern_run!` as blocking/convenience composition over start + wait/sync.

Schematic target, not final syntax:

```edict
[@task
  task intern.prepare_task! @spec
  spec.max_active_jobs intern.active_count! intern.backpressure? & [ spec intern.backpressure_envelope! ] |
  spec root1.participant_register! @participant
  spec participant worker.edict_start! @worker
  spec participant worker intern.start_envelope!
] @intern_start

[@job
  job intern.lookup_or_handle! @record
  record.participant root1.poll! /
  record.participant root1.mailbox_drain! @events
  record.worker worker.edict_collect_if_ready! @worker_status
  record events worker_status intern.sync_envelope!
] @intern_sync

['cancel intern_sync!] @intern_cancel

[@task
  task intern_start! @job
  job.waitable root1.await! /
  job intern_sync!
] @intern_run
```

## Migration Plan

### Phase 0 — Freeze current behavior

- [x] Keep the current source-control checkpoint as the baseline.
- [x] Record focused behavior expectations for `intern_run!`, `intern_start!`, `intern_sync!`, `intern_cancel!`, backpressure, unknown-job, and shared read-only context mutation refusal.
- [x] Add or preserve tests that will be reused against the module-backed implementation.

### Phase 1 — Extract native services from VM op methods

- [x] Move the current `InternJobManager`, worker join slot, worker execution helper, descriptor conversion, and Root1 broker interaction out of direct `EdictVM::op_INTERN_*` method bodies into a native service boundary usable by FFI exports. The surviving status/fact service functions live in `edict_worker_primitives.cpp`; fresh worker-VM execution and join-slot transfer live in `edict_worker_runtime.{h,cpp}`; the thin C ABI/LTV wrappers live in `edict_worker_primitives_ffi.cpp`.
- [x] Keep behavior identical and keep the existing VM opcodes as compatibility shims during extraction.
- [x] Ensure the native service has no dependency on cpp-agent provider/runtime state.

### Phase 2 — Add importable primitive C ABI

- [x] Create an importable C header for worker primitives: `edict/agentc_worker_primitives.h`.
- [x] Expose minimal LTV primitive functions via Cartographer FFI: `agentc_worker_edict_active_count_ltv`, `agentc_worker_edict_prepare_task_ltv`, `agentc_worker_edict_capacity_status_ltv`, `agentc_worker_edict_run_status_ltv`, `agentc_worker_edict_run_status_prepared_ltv`, `agentc_worker_edict_start_status_ltv`, `agentc_worker_edict_start_status_prepared_ltv`, `agentc_worker_edict_drain_events_ltv`, `agentc_worker_edict_request_cancel_ltv`, `agentc_worker_edict_collect_status_ltv`, and `agentc_worker_edict_drop_ltv`. This slice exports them from `libedict.so`; a later slice may split them into a smaller `libagentc_worker_primitives.so`.
- [x] Add focused C++/Edict tests that import the library explicitly and exercise module-backed words without relying on direct `VMOP_INTERN_*` invocation for the public surface.
- [x] Keep raw fd/eventfd/epoll state hidden behind logical ids, waitables, and descriptor objects.
- [x] Add first importable Root1 primitive header `edict/agentc_root1_primitives.h` with LTV exports for participant registration, poll, mailbox send/drain, cancellation descriptor send, and backpressure descriptor send. This first slice also exports from `libedict.so` and can split later into a dedicated Root1 primitive library.

### Phase 3 — Add Edict primitive modules

- [x] Add first `root1.edict` wrappers for participant registration, polling, descriptor send, mailbox drain, cancellation descriptors, and backpressure descriptors. Await/resource-acquire wrappers remain later G110/G111 slices.
- [x] Add first `worker.edict` wrappers for imported active-count, prepare-task, capacity-status, run-status, start-status, drain-events, request-cancel, collect-status, and drop primitives. Older envelope-returning native run/start/collect/sync/cancel/check-capacity exports have been removed from the imported module/header surface.
- [x] Use adjacent eval sigils and `/` discard style consistently.
- [x] Document low-level words as substrate API, not the normal user surface.

### Phase 4 — Rebuild intern words in Edict

- [x] Add first `intern.edict` with public `intern_start!`, `intern_sync!`, `intern_cancel!`, and `intern_run!` over worker primitives. `intern_run!` now composes `worker.edict_prepare_task!`, `worker.edict_run_status_prepared!`, and Edict-owned `intern.run_envelope`; `intern_start!` composes `worker.edict_prepare_task!`, `worker.edict_capacity_status!`, Edict-owned `intern.backpressure_envelope`, `worker.edict_start_status_prepared!`, and Edict-owned `intern.start_envelope`; `intern_sync!` composes `worker.edict_drain_events!`, `worker.edict_collect_status!`, and Edict-owned `intern.sync_envelope`; and `intern_cancel!` composes `worker.edict_request_cancel!`, `worker.edict_collect_status!`, and Edict-owned `intern.sync_envelope`.
- [x] Move public envelope construction into Edict for blocking run, start, backpressure, running, cancel-requested, complete/error, cancelled, and unknown-job states. The native status primitives now return job facts, result/safety values, logical waitables, and descriptor events; `intern.edict` owns the public envelope object shape. Native hard caps remain transitional.
- [x] Keep the public surface compatible with the current examples:

  ```edict
  task intern_start! @job
  job.job_id intern_sync! @status
  job.job_id intern_cancel! @cancel_status
  task intern_run! @result
  ```

### Phase 5 — Port tests/docs and retire VM opcodes

- [x] Port `InternWorkerTest.*` to the imported-module implementation; direct public intern tests now load `worker.edict` / `intern.edict` over imported primitives. CLI smoke tests remain a possible follow-up if the raw launcher should preload intern modules.
- [x] Remove temporary opcode-backed compatibility tests and add coverage proving raw VM startup no longer installs `intern_run`, `intern_start`, `intern_sync`, or `intern_cancel` as bootstrap builtins.
- [x] Remove `VMOP_INTERN_RUN`, `VMOP_INTERN_START`, and `VMOP_INTERN_SYNC` from `edict/edict_types.h`, VM dispatch, VM method declarations/definitions, and bootstrap builtin/capsule registration. `main.cpp` deliberately still strips legacy persisted `intern_*` names during session restore so stale opcode thunks from older sessions cannot shadow module-backed words.
- [x] Rename/repurpose `edict/edict_vm_intern.cpp` to `edict/edict_worker_primitives.cpp` as the transitional worker service implementation; intern behavior no longer ships as VM dispatch.
- [x] Split fresh worker-VM execution/join-slot transfer into `edict/edict_worker_runtime.{h,cpp}`.
- [x] Split thin C ABI / Cartographer LTV wrappers into `edict/edict_worker_primitives_ffi.cpp`, leaving `edict_worker_primitives.cpp` focused on the async job table, task/status facts, and Root1 broker plumbing.
- [x] Update README, 🔗[K032 — Edict Intern Worker Surface](../../Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md), and 🔗[Edict Language Reference](../../WorkProducts/edict_language_reference.md).

### Phase 6 — Follow-on generalization handoff

- [x] Hand generic `await!` semantics over Root1 waitables to 🔗[G110](../G110-EventfdEpollMicroVmIpcDesign/index.md).
- [x] Hand Edict-level intern task-quality contracts to 🔗[G099](../G099-InternTaskQualityContracts/index.md); no native opcode checks remain.
- [x] Hand process-worker reuse of the same Root1/worker primitive surface to 🔗[G107](../G107-ProcessIsolatedMicroVmInterns/index.md).

## Implementation Progress — 2026-05-17

First migration slice landed:

- Added `edict/edict_intern_service.h` and moved direct opcode method bodies behind `agentc::edict::intern::{run,start,sync,cancel}` service functions.
- Initially kept `VMOP_INTERN_RUN`, `VMOP_INTERN_START`, and `VMOP_INTERN_SYNC` as temporary compatibility shims that delegated to the service boundary; later slices removed those shims from VM enum/dispatch/bootstrap.
- Added `edict/agentc_worker_primitives.h` plus C ABI LTV exports from `libedict.so`:
  - `agentc_worker_edict_active_count_ltv`
  - `agentc_worker_edict_prepare_task_ltv`
  - `agentc_worker_edict_run_status_ltv`
  - `agentc_worker_edict_run_status_prepared_ltv`
  - `agentc_worker_edict_start_status_ltv`
  - `agentc_worker_edict_start_status_prepared_ltv`
  - `agentc_worker_edict_drain_events_ltv`
  - `agentc_worker_edict_request_cancel_ltv`
  - `agentc_worker_edict_collect_status_ltv`
  - `agentc_worker_edict_drop_ltv`
- Added `cpp-agent/edict/modules/worker.edict` as the low-level wrapper namespace over imported worker primitives.
- Added `cpp-agent/edict/modules/intern.edict` as the first module-backed public intern surface. `intern_run!` now composes prepare-task plus run-status plus Edict run-envelope shaping, `intern_start!` composes prepare-task plus capacity-status/backpressure-envelope plus start-status plus Edict start-envelope shaping, `intern_sync!` composes the lower-level `worker.edict_drain_events!`, `worker.edict_collect_status!`, and Edict envelope-shaping words, and `intern_cancel!` composes `worker.edict_request_cancel!`, `worker.edict_collect_status!`, and Edict envelope-shaping words.
- Added `InternWorkerTest.ModuleBackedInternWordsUseImportedWorkerPrimitives`, which imports `libedict.so`/`agentc_worker_primitives.h`, loads `worker.edict` and `intern.edict`, then proves module-backed `intern_run!`, `intern_start!`, `intern_sync!`, `intern_cancel!`, active-count, task preparation, capacity-status facts, module-level backpressure branching, direct low-level drain/collect-status behavior, direct lower-level request-cancel behavior, and explicit drop cleanup.

Recent G111 slices added `agentc_worker_edict_collect_status_ltv` / `worker.edict_collect_status!`, then `agentc_worker_edict_run_status_*` / `worker.edict_run_status*` and `agentc_worker_edict_start_status_*` / `worker.edict_start_status*`, so native worker operations can return lower-level facts instead of public envelopes. `intern.edict` now builds the public run/start/sync/cancel envelopes for blocking `complete`/`error`, `started`, `backpressure`, `running`, `cancel_requested`, final `complete`/`error`, final `cancelled`, and unknown-job states, while still using native primitives for the worker table, thread/fresh-VM execution, coordinator-thread result materialization, logical waitables, and descriptor event drain/request mechanics.

This closes the G111 migration: the public words are module-backed through FFI, public envelopes are Edict-shaped, active-count is exposed, task preparation and start-time capacity/backpressure branching are composed in `intern.edict`, explicit drop cleanup exists, first generic `root1.edict` participant/poll/mailbox wrappers exist, compatibility VM opcodes have been deleted, legacy native public-envelope service paths have been removed, fresh worker-VM execution is split into `edict_worker_runtime.{h,cpp}`, and the C ABI/LTV wrapper layer is split into `edict_worker_primitives_ffi.cpp`. Native hard worker caps and raw lifecycle cleanup policy are now G091/G110 follow-ups rather than G111 blockers.

Validation:

- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*'` — passed 8/8.
- Focused Edict regression slice `FreezeBuiltin.*:InternWorkerTest.*:EdictVM.*:VMStackTest.*:SimpleAssignTest.*:RegressionMatrixTest.*:PiSimulationTest.MiniKanrenLogicExample` — passed 54/54.
- `cmake --build build --target reflect_tests cpp_agent_tests -j2` — passed.
- `./build/tests/reflect_tests --gtest_brief=1` — passed 43/43.
- `./build/cpp-agent/cpp_agent_tests --gtest_brief=1` — passed 49/49.
- After splitting module-backed `intern_sync!` into Edict-level drain/collect composition, splitting module-backed `intern_cancel!` into request-cancel/collect composition, adding the active-count primitive, and adding explicit drop cleanup, repeated validation passed: `InternWorkerTest.*` 8/8, focused Edict slice 54/54, `reflect_tests` 43/43, and `cpp_agent_tests` 49/49.
- After adding prepare-task, capacity, run-prepared, and start-prepared primitives, repeated validation passed: `cmake --build build --target edict_tests -j2`, `InternWorkerTest.*` 8/8, focused Edict slice 54/54, `cmake --build build --target reflect_tests cpp_agent_tests -j2`, `reflect_tests` 43/43, and `cpp_agent_tests` 49/49.
- After adding `worker.edict_capacity_status!`, Edict-owned `intern.backpressure_envelope`, `agentc_root1_primitives.h`, `cpp-agent/edict/modules/root1.edict`, and `Root1PrimitiveModuleTest.ModuleBackedParticipantMailboxPrimitives`, validation passed: `cmake --build build --target edict_tests -j2`, `InternWorkerTest.*:Root1PrimitiveModuleTest.*` 9/9, focused Edict slice with `Root1PrimitiveModuleTest.*` 55/55, `cmake --build build --target reflect_tests cpp_agent_tests -j2`, `reflect_tests` 43/43, and `cpp_agent_tests` 49/49.
- After adding `worker.edict_collect_status!` and moving async sync/cancel public envelope construction into `intern.edict`, validation passed: `cmake --build build --target edict_tests -j2`, `InternWorkerTest.ModuleBackedInternWordsUseImportedWorkerPrimitives`, `InternWorkerTest.*:Root1PrimitiveModuleTest.*` 9/9, focused Edict slice with `Root1PrimitiveModuleTest.*` 55/55, `cmake --build build --target reflect_tests cpp_agent_tests -j2`, `reflect_tests` 43/43, and `cpp_agent_tests` 49/49.
- After deleting `VMOP_INTERN_RUN`, `VMOP_INTERN_START`, and `VMOP_INTERN_SYNC`, validation passed: `cmake --build build --target edict_tests -j2`, `InternWorkerTest.*:Root1PrimitiveModuleTest.*` 10/10, focused Edict slice with `Root1PrimitiveModuleTest.*` 56/56, `cmake --build build --target reflect_tests cpp_agent_tests -j2`, `reflect_tests` 43/43, and `cpp_agent_tests` 49/49.
- After adding run-status/start-status primitives, moving run/start public envelopes into `intern.edict`, removing envelope-returning raw worker words from `worker.edict` and `agentc_worker_primitives.h`, and renaming the native implementation to `edict_worker_primitives.cpp`, validation passed: `cmake --build build --target edict_tests -j2`, `InternWorkerTest.*:Root1PrimitiveModuleTest.*` 10/10, focused Edict slice with `Root1PrimitiveModuleTest.*` 56/56, `cmake --build build --target reflect_tests cpp_agent_tests -j2`, `reflect_tests` 43/43, and `cpp_agent_tests` 49/49.
- After removing the remaining legacy envelope-returning service path (`run`, `runPrepared`, `start`, `startPrepared`, `collect`, `sync`, `cancel`), removing the redundant raw `check_capacity` primitive from the imported worker surface, splitting C ABI/LTV wrappers into `edict_worker_primitives_ffi.cpp`, and splitting fresh worker VM execution into `edict_worker_runtime.{h,cpp}`, validation passed: `cmake --build build --target edict_tests -j2`, `InternWorkerTest.*:Root1PrimitiveModuleTest.*` 10/10, the focused Edict slice 56/56, `cmake --build build --target reflect_tests cpp_agent_tests -j2`, `reflect_tests` 43/43, and `cpp_agent_tests` 49/49.

## Acceptance Criteria

- [x] Public `intern_run!`, `intern_start!`, `intern_sync!`, and `intern_cancel!` can be loaded as plain Edict words from module code over imported worker primitives.
- [x] No intern-specific VM opcodes remain in the core VM enum or dispatch loop.
- [x] Native implementation is limited to importable Root1/worker mechanisms that Edict cannot implement directly: fresh worker VM/thread execution, coordinator-thread result materialization, process-local job table/waitable state, Root1 broker descriptor drain/publication, and hard native safety caps.
- [x] Low-level primitive words are importable and testable independently of direct VM opcode invocation.
- [x] Intern public envelope shaping, cancellation composition, and backpressure policy are implemented in Edict; safety-critical task preparation/freeze/snapshot remains a low-level worker primitive boundary.
  - [x] Task-preparation and start-time capacity/backpressure branching are now composed in `intern.edict` over low-level worker primitives.
  - [x] Public backpressure envelope construction now lives in `intern.edict` over a lower-level native capacity-status primitive.
  - [x] Running, final, cancellation-final, and unknown-job public async sync/cancel envelope construction now lives in `intern.edict` over lower-level native collect-status facts.
  - [x] Blocking run and async start public envelope construction now lives in `intern.edict` over lower-level native run-status/start-status facts.
  - [x] Hard safety caps are explicitly classified as native safeguards; broader worker lifecycle cleanup is handed to 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md) rather than blocking G111.
- [x] Existing G091 safety invariants remain true: coordinator-owned Listree state is mutated only on the coordinator thread; worker `input` is snapshotted; shared `context`/`imports` are read-only; worker result is copied/collected safely.
- [x] Focused regressions pass for blocking run, async start/sync, cancellation, backpressure, unknown job, and shared-context mutation refusal.

## Risks / Open Questions

- **LTV vs JSON FFI boundary**: JSON primitives are easier to import and safer across threads, but LTV worker-start primitives may be needed to preserve G091's read-only shared-context behavior without copying large context/import subtrees.
- **Native handle lifetime**: Edict should store logical ids/handles, not raw pointers/fds. Process-lifetime registries are acceptable for the first slice, but G096/G110 resume work must later define reconstruction/invalid-handle semantics.
- **Backpressure authority**: Edict should own task-level `max_active_jobs`, but native worker primitives still need hard safety caps and structured hard-cap failures.
- **Cancellation depth**: Current cancellation is cooperative and result-suppressing. A future worker-visible cancellation flag can be exposed as a primitive, but preemptive thread kill remains out of scope.
- **Bootstrapping path**: Decide whether raw `edict` preloads these words, whether only the curated launcher preloads them, or whether tests/examples import them explicitly. The VM should not need intern-specific bootstrap opcodes either way.
