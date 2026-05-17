# Goal: G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration

**Status**: ACTIVE / NEXT IMPLEMENTATION TRACK
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

It is acceptable, and desirable, to expose low-level primitive words such as `root1.mailbox_drain!` or `worker.edict_collect!` even if normal users only call `intern_sync!`. The low-level words make the substrate composable for future provider streams, process-isolated workers, explicit mailboxes, and `await!` without forcing every new async feature into VM dispatch.

## Current State

- `VMOP_INTERN_CANCEL` has already been removed.
- `intern_cancel!` is a plain bootstrap Edict word over `intern_sync!`:

  ```edict
  ['cancel intern_sync!] @intern_cancel
  ```

- The remaining intern-specific opcodes are:
  - `VMOP_INTERN_RUN`
  - `VMOP_INTERN_START`
  - `VMOP_INTERN_SYNC`
- Their implementation currently lives in `edict/edict_vm_intern.cpp` and mixes multiple layers:
  - intern task parsing and envelope shaping;
  - worker VM lifecycle;
  - process-local async job table;
  - G110 `Root1ResourceBroker` descriptor publication/drain;
  - cancellation/backpressure policy.

G111 separates those layers so only the irreducible native primitives remain in C/C++ libraries, and intern policy moves into Edict modules.

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

- [x] Move the current `InternJobManager`, worker join slot, worker execution helper, descriptor conversion, and Root1 broker interaction out of direct `EdictVM::op_INTERN_*` method bodies into a native service boundary usable by VM shims or FFI exports. First slice exposes `agentc::edict::intern::{run,start,sync,cancel}` in `edict/edict_intern_service.h`; the implementation still lives in `edict_vm_intern.cpp` and should be split to a dedicated translation unit in a later cleanup.
- [x] Keep behavior identical and keep the existing VM opcodes as compatibility shims during extraction.
- [x] Ensure the native service has no dependency on cpp-agent provider/runtime state.

### Phase 2 — Add importable primitive C ABI

- [x] Create an importable C header for worker primitives: `edict/agentc_worker_primitives.h`.
- [x] Expose minimal LTV primitive functions via Cartographer FFI: `agentc_worker_edict_run_ltv`, `agentc_worker_edict_start_ltv`, `agentc_worker_edict_sync_ltv`, and `agentc_worker_edict_cancel_ltv`. This first slice exports them from `libedict.so`; a later slice may split them into a smaller `libagentc_worker_primitives.so`.
- [x] Add focused C++/Edict tests that import the library explicitly and exercise module-backed words without relying on direct `VMOP_INTERN_*` invocation for the public surface.
- [x] Keep raw fd/eventfd/epoll state hidden behind logical ids, waitables, and descriptor objects.

### Phase 3 — Add Edict primitive modules

- [ ] Add `root1.edict` wrappers for participant registration, polling/awaiting, descriptor send, and mailbox drain.
- [x] Add first `worker.edict` wrappers for imported fresh Edict-worker run/start/sync/cancel primitives.
- [x] Use adjacent eval sigils and `/` discard style consistently.
- [x] Document low-level words as substrate API, not the normal user surface.

### Phase 4 — Rebuild intern words in Edict

- [x] Add first `intern.edict` with public `intern_start!`, `intern_sync!`, `intern_cancel!`, and `intern_run!` over worker primitives. `intern_prepare_task!` and deeper Edict-owned policy remain future work.
- [ ] Move envelope construction and backpressure/cancellation policy into Edict; current module words still delegate those policies to transitional worker primitives.
- [x] Keep the public surface compatible with the current examples:

  ```edict
  task intern_start! @job
  job.job_id intern_sync! @status
  job.job_id intern_cancel! @cancel_status
  task intern_run! @result
  ```

### Phase 5 — Port tests/docs and retire VM opcodes

- [ ] Port `InternWorkerTest.*` and CLI smoke tests to the imported-module implementation.
- [ ] Keep temporary opcode-backed compatibility tests only until the module path is stable.
- [ ] Remove `VMOP_INTERN_RUN`, `VMOP_INTERN_START`, and `VMOP_INTERN_SYNC` from `edict/edict_types.h`, VM dispatch, bootstrap builtins, and volatile startup cleanup.
- [ ] Remove or repurpose `edict/edict_vm_intern.cpp` so intern behavior no longer ships as VM dispatch.
- [ ] Update README, 🔗[K032 — Edict Intern Worker Surface](../../Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md), and 🔗[Edict Language Reference](../../WorkProducts/edict_language_reference.md).

### Phase 6 — Follow-on generalization

- [ ] Add generic `await!` semantics over Root1 waitables under 🔗[G110](../G110-EventfdEpollMicroVmIpcDesign/index.md).
- [ ] Let 🔗[G099](../G099-InternTaskQualityContracts/index.md) consume the Edict-level task validator/envelope helpers rather than native opcode checks.
- [ ] Let 🔗[G107](../G107-ProcessIsolatedMicroVmInterns/index.md) reuse the same Root1/worker primitives with process workers instead of thread workers.

## Implementation Progress — 2026-05-17

First migration slice landed:

- Added `edict/edict_intern_service.h` and moved direct opcode method bodies behind `agentc::edict::intern::{run,start,sync,cancel}` service functions.
- Kept `VMOP_INTERN_RUN`, `VMOP_INTERN_START`, and `VMOP_INTERN_SYNC` as temporary compatibility shims that delegate to the service boundary.
- Added `edict/agentc_worker_primitives.h` plus C ABI LTV exports from `libedict.so`:
  - `agentc_worker_edict_run_ltv`
  - `agentc_worker_edict_start_ltv`
  - `agentc_worker_edict_sync_ltv`
  - `agentc_worker_edict_cancel_ltv`
- Added `cpp-agent/edict/modules/worker.edict` as the low-level wrapper namespace over imported worker primitives.
- Added `cpp-agent/edict/modules/intern.edict` as the first module-backed public intern surface.
- Added `InternWorkerTest.ModuleBackedInternWordsUseImportedWorkerPrimitives`, which imports `libedict.so`/`agentc_worker_primitives.h`, loads `worker.edict` and `intern.edict`, then proves module-backed `intern_run!`, `intern_start!`, `intern_sync!`, and `intern_cancel!` behavior.

This is intentionally transitional: the public words can now be module-backed through FFI, but validation/envelope/backpressure/cancellation policy still lives in the native worker primitive implementation. The next migration slice should split lower-level Root1/worker operations so more policy can move into Edict.

Validation:

- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*'` — passed 8/8.
- Focused Edict regression slice `FreezeBuiltin.*:InternWorkerTest.*:EdictVM.*:VMStackTest.*:SimpleAssignTest.*:RegressionMatrixTest.*:PiSimulationTest.MiniKanrenLogicExample` — passed 54/54.
- `cmake --build build --target reflect_tests cpp_agent_tests -j2` — passed.
- `./build/tests/reflect_tests --gtest_brief=1` — passed 43/43.
- `./build/cpp-agent/cpp_agent_tests --gtest_brief=1` — passed 49/49.

## Acceptance Criteria

- [x] Public `intern_run!`, `intern_start!`, `intern_sync!`, and `intern_cancel!` can be loaded as plain Edict words from module code over imported worker primitives.
- [ ] No intern-specific VM opcodes remain in the core VM enum or dispatch loop.
- [ ] Native implementation is limited to generic Root1/worker primitives that Edict cannot implement directly.
- [x] Low-level primitive words are importable and testable independently of direct VM opcode invocation.
- [ ] Intern task validation, envelope shaping, cancellation, and backpressure policy are implemented in Edict.
- [x] Existing G091 safety invariants remain true: coordinator-owned Listree state is mutated only on the coordinator thread; worker `input` is snapshotted; shared `context`/`imports` are read-only; worker result is copied/collected safely.
- [x] Focused regressions pass for blocking run, async start/sync, cancellation, backpressure, unknown job, and shared-context mutation refusal.

## Risks / Open Questions

- **LTV vs JSON FFI boundary**: JSON primitives are easier to import and safer across threads, but LTV worker-start primitives may be needed to preserve G091's read-only shared-context behavior without copying large context/import subtrees.
- **Native handle lifetime**: Edict should store logical ids/handles, not raw pointers/fds. Process-lifetime registries are acceptable for the first slice, but G096/G110 resume work must later define reconstruction/invalid-handle semantics.
- **Backpressure authority**: Edict should own task-level `max_active_jobs`, but native worker primitives still need hard safety caps and structured hard-cap failures.
- **Cancellation depth**: Current cancellation is cooperative and result-suppressing. A future worker-visible cancellation flag can be exposed as a primitive, but preemptive thread kill remains out of scope.
- **Bootstrapping path**: Decide whether raw `edict` preloads these words, whether only the curated launcher preloads them, or whether tests/examples import them explicitly. The VM should not need intern-specific bootstrap opcodes either way.
