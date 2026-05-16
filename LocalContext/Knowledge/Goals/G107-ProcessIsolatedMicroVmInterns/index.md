# Goal: G107 — Process-Isolated Micro-VM Interns

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Run intern workers as process-isolated micro-VMs that mmap the static core image read-only, own private dynamic overlays, and communicate with Root1 through async job events plus optional advertised read-only result slabs.

## Rationale
Threaded workers prove the control plane, but process isolation is the stronger safety and sharing model. Separate worker processes can map core slabs read-only at the OS level, use tiny private writable overlays, and publish immutable results without sharing mutable VM/Listree state. 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md) records the likely Linux waitable/ownership-broker substrate for process-isolated mailbox IPC.

## Implementation Plan
- [ ] Define the micro-VM launch contract: image manifest, task envelope, leased slab range/layer, and output channel.
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

## Dependencies
- Requires the async control-plane slice of 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md).
- Likely depends on 🔗[G099](../G099-InternTaskQualityContracts/index.md), 🔗[G092](../G092-CartographerFfiReentrancyMetadata/index.md), 🔗[G103](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md), 🔗[G105](../G105-ReadOnlyStaticSlabOwnershipModel/index.md), 🔗[G106](../G106-Root1SlabAdvertisementRegistry/index.md), and 🔗[G110](../G110-EventfdEpollMicroVmIpcDesign/index.md).
