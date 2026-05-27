# Goal: G107 — Process-Isolated Micro-VM Interns

**Status**: ACTIVE / STATIC-MOUNT CONTRACT SMOKE
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Run intern workers as process-isolated micro-VMs that mmap the static core image read-only, own private dynamic overlays, and communicate with Root1 through async job events plus optional advertised read-only result slabs.

## Rationale
Threaded workers prove the control plane, but process isolation is the stronger safety and sharing model. Separate worker processes can map core slabs read-only at the OS level, use tiny private writable overlays, and publish immutable results without sharing mutable VM/Listree state. 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md) records the likely Linux waitable/ownership-broker substrate for process-isolated mailbox IPC.

## Implementation Plan
- [x] Add a pre-isolation end-to-end smoke where a launched worker executes a shared frozen/static-immortal base code thunk.
- [x] Define the first pre-process launch contract seam for static mount descriptors in the task envelope.
- [ ] Define the full micro-VM launch contract: image manifest, task envelope, leased slab range/layer, and output channel.
- [ ] Support launching a worker process that mounts the static core declaration image or a test image.
- [ ] Preserve `intern_start!` / `intern_sync!` semantics across the process boundary.
- [ ] Ensure stateful provider/runtime handles are not inherited or are explicitly rehydrated only when allowed.
- [ ] Add tests/smokes for a deterministic process-isolated worker returning a structured result.
- [ ] Consider a Root1 forkserver mode as an intermediate only if inherited handle/thread risks are controlled.

## Acceptance Criteria
- [ ] A deterministic intern task can run in a separate process and return a structured result to Root1/coordinator.
- [ ] The worker maps shared static/core slabs read-only and uses private dynamic state for execution.
- [ ] Worker publication or result transfer does not require mutable coordinator-owned Listree access from the worker.
- [ ] Documentation states which handles/capabilities may be inherited, rehydrated, or blocked.

## Progress
- 2026-05-27: Added the first pre-isolation shared-code worker smoke in `InternWorkerTest.WorkerExecutesSharedStaticBaseCodeThunk`. The coordinator builds a frozen base thunk in a shared context, marks the context root and thunk slots static-immortal, launches an async intern worker through `intern_start!`, and the worker executes `context.base!` to populate its private result from shared code plus private input. This is intentionally still the thread-worker backend, not the final process-isolated OS-mmap worker.
- 2026-05-27: Validation passed: `cmake --build build --target edict_tests -j2`; focused smoke `InternWorkerTest.WorkerExecutesSharedStaticBaseCodeThunk` 1/1; focused intern/shared-code slice `InternWorkerTest.WorkerExecutesSharedStaticBaseCodeThunk:InternWorkerTest.InternRunDispatchesWorkerAndCollectsStructuredResult:InternWorkerTest.InternStartAndSyncCollectsStructuredResultAsynchronously` 3/3; broader worker/G104 slice `InternWorkerTest.*:EdictVM.FrozenThunkUsesPrivateInstructionPointerAcrossResumeAndReuse:FreezeBuiltin.*:SharingTest.*` 27/27.
- 2026-05-27: Added the first static-image/mmap-backed worker launch-contract seam. `InternWorkerInput` now carries `staticMountsReadOnly`; task parsing freezes optional `static_mounts`, prepared specs preserve it, worker VM roots expose it as `static_mounts`, and safety envelopes report `static_mounts_read_only`. New `InternWorkerTest.WorkerExecutesSharedBaseWithMmapStaticMountContract` writes a G103 worker declaration-image container, reads it through `PROT_READ` mmap, mounts it through the exact-slot static-immortal path, passes a handle-free static mount descriptor in `task.static_mounts`, launches an async worker, and proves the worker both executes shared `context.base!` code and reads the static image descriptor. This still uses the thread-worker backend and descriptor metadata; it is not yet a process-isolated worker or allocator-mounted static code slab.
- 2026-05-27: Validation passed: `cmake --build build --target edict_tests -j2`; focused mmap-contract smoke `InternWorkerTest.WorkerExecutesSharedBaseWithMmapStaticMountContract` 1/1; focused worker/static-image slice `InternWorkerTest.WorkerExecutesSharedStaticBaseCodeThunk:InternWorkerTest.WorkerExecutesSharedBaseWithMmapStaticMountContract:InternWorkerTest.InternRunDispatchesWorkerAndCollectsStructuredResult:InternWorkerTest.InternStartAndSyncCollectsStructuredResultAsynchronously:StaticDeclarationImageTest.*` 14/14. Broader worker/G104 slice printed 28/28 passed but the shell metadata reported a timeout after output, matching the existing async intern test-runner quirk, so the focused slices are the relied-on validation.

## Dependencies
- Requires the async control-plane slice of 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md).
- Likely depends on 🔗[G099](../G099-InternTaskQualityContracts/index.md), 🔗[G092](../G092-CartographerFfiReentrancyMetadata/index.md), 🔗[G103](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md), 🔗[G105](../G105-ReadOnlyStaticSlabOwnershipModel/index.md), 🔗[G106](../G106-Root1SlabAdvertisementRegistry/index.md), and 🔗[G110](../G110-EventfdEpollMicroVmIpcDesign/index.md).
