# Goal: G106 — Root1 Slab Advertisement Registry

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Design and implement the first Root1-owned registry for advertised worker slab publications: slab ranges/layers, manifest paths, root ids, epochs, permissions, and lifecycle leases. This should integrate with 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md): Root1's publication directory, resource keys, waitable/mailbox registry, ownership epochs, and lifecycle leases should use one coherent control-plane model.

## Rationale
The full-send architecture needs a coordinator-owned way for tertiary micro-VMs to publish immutable result/context slabs without mutating a global shared Listree directly. Root1 should own the registry that decides which published roots become visible and prevents slab-id collisions.

## Candidate Advertisement Fields
- owner VM/process id;
- leased slab id range or layer id;
- slab file paths and content hashes;
- published root `SlabId`/handle;
- read/write permissions;
- publication epoch/version;
- immutable/final vs owner-private state;
- lifecycle/garbage-collection lease policy.

## Implementation Plan
- [ ] Define the first advertisement/lease schema.
- [ ] Decide whether the MVP uses absolute slab-id range leasing or logical layer ids.
- [ ] Add Root1/coordinator registry data structures.
- [ ] Add APIs for worker publication registration and read-only consumer lookup.
- [ ] Add manifest/hash validation before accepting a publication.
- [ ] Add tests for collision rejection, stale manifest rejection, and successful publication lookup.

## Acceptance Criteria
- [ ] Root1 can lease/register a worker publication without exposing mutable global state to the worker.
- [ ] Slab id collisions are rejected or impossible under the chosen scheme.
- [ ] Consumers receive enough metadata to mmap/read a publication as read-only.
- [ ] Lifecycle/GC semantics are documented at least at the lease/epoch level.

## Dependencies
- Comes after the Root1 broker design in 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md), because G106 should reuse its resource-key, lease, epoch, and participant identity model.
- Likely follows 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md) for immutable publication safety.
