# Goal: G106 — Root1 Slab Advertisement Registry

**Status**: ACTIVE — logical-layer publication registry MVP landed; manifest content validation and worker/public API integration remain<br>
**Created**: 2026-05-14<br>
**Updated**: 2026-06-20<br>
**Parent**: 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md)<br>
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Design and implement the first Root1-owned registry for advertised worker slab publications: slab ranges/layers, manifest paths, root ids, epochs, permissions, and lifecycle leases. This should integrate with 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md): Root1's publication directory, resource keys, waitable/mailbox registry, ownership epochs, and lifecycle leases should use one coherent control-plane model.

## Rationale
The full-send architecture needs a coordinator-owned way for tertiary micro-VMs to publish immutable result/context slabs without mutating a global shared Listree directly. Root1 should own the registry that decides which published roots become visible and prevents slab-id collisions.

## Current Status — 2026-06-20
The first Root1 broker-owned publication registry MVP has landed in `core/root1_resource_broker.{h,cpp}`.

Design decision: the MVP uses **Root1-assigned logical publication layer ids**, not absolute slab-id range leasing. This makes worker slab-id collisions impossible at the registry boundary: workers publish metadata into a Root1-owned logical layer, and consumers receive layer/manifest/root metadata for read-only mapping rather than direct mutable global state.

Implemented surface:

- `PublicationLayerId` logical id type.
- `PublicationPermission::ReadOnly` and `PublicationDescriptor` metadata schema.
- `Root1ResourceBroker::leasePublicationLayer(owner, expiresAtTick)`.
- `Root1ResourceBroker::renewPublicationLease(layerId, owner, expiresAtTick)`.
- `Root1ResourceBroker::registerPublication(publication, error)`.
- `Root1ResourceBroker::lookupPublication(layerId)` for consumer metadata lookup.
- `Root1ResourceBroker::retirePublication(layerId, owner)`.
- `Root1ResourceBroker::recoverExpiredPublicationLeases(nowTick)` to withdraw expired publication visibility.

Validation currently enforces owner/layer lease matching, non-empty manifest path/hash fields, read-only immutable publication metadata, root-handle layer consistency, and monotonic publication epochs. Full manifest-file opening/hash verification remains unchecked future work.

## Candidate Advertisement Fields
- owner VM/process id;
- leased logical layer id;
- slab file paths and content hashes via manifest path/hash;
- published root handle / descriptor;
- read-only permission;
- publication epoch/version;
- immutable/final publication state;
- lifecycle/garbage-collection lease expiry.

## Implementation Plan
- [x] Define the first advertisement/lease schema.
- [x] Decide whether the MVP uses absolute slab-id range leasing or logical layer ids — **chosen: logical layer ids**.
- [x] Add Root1/coordinator registry data structures.
- [x] Add APIs for worker publication registration and read-only consumer lookup.
- [ ] Add manifest/hash validation before accepting a publication beyond required manifest path/hash fields.
- [x] Add tests for collision rejection, stale epoch rejection, and successful publication lookup.

## Acceptance Criteria
- [x] Root1 can lease/register a worker publication without exposing mutable global state to the worker.
- [x] Slab id collisions are rejected or impossible under the chosen scheme.
- [x] Consumers receive enough metadata to mmap/read a publication as read-only.
- [x] Lifecycle/GC semantics are documented at least at the lease/epoch level.
- [ ] Publication registration validates the manifest file/hash rather than only requiring manifest metadata fields.

## Verification — 2026-06-20
- `cmake --build build --target reflect_tests -j$(nproc)` passed.
- Focused new G106 tests passed 3/3:
  - `Root1ResourceBrokerTest.LeasesLogicalPublicationLayerAndPublishesReadOnlyManifest`
  - `Root1ResourceBrokerTest.PublicationRegistryRejectsCollisionsAndStaleEpochs`
  - `Root1ResourceBrokerTest.ExpiredPublicationLeaseWithdrawsConsumerVisibility`
- Full `Root1ResourceBrokerTest.*` passed 21/21.
- Full `reflect_tests` passed 54/54.
- Affected Edict Root1/static-image slice passed 26/26: `Root1PrimitiveModuleTest.*:Root1AwaitSchedulerTest.*:StaticDeclarationImageTest.*`.
- Affected build targets passed: `reflect_tests`, `edict_tests`, `cpp_agent_tests`.

## Next Steps
1. Add real manifest-file validation: open manifest, verify content hash, validate root handle/layer metadata against the advertised descriptor.
2. Decide whether publication lookup should emit/attach `MailboxPayloadKind::PublishedSlab` descriptors for waiting consumers.
3. Add Edict/module-facing Root1 publication primitives only after the C++ registry semantics remain stable.
4. Integrate process-isolated worker publication handoff from G107-style worker outcomes into this registry.

## Dependencies
- Comes after the Root1 broker design in 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md), because G106 reuses its participant identity, lease, and epoch model.
- Follows 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md) for immutable publication safety.
- Builds on 🔗[G096 — Authoritative mmap Session Resume](../G096-AuthoritativeMmapSessionResume/index.md) for session/static-mount resume boundaries.
- Depends on G107 process-isolated workers as the primary future publisher.
