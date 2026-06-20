# Goal: G106 — Root1 Slab Advertisement Registry

**Status**: COMPLETE — Root1 logical publication layer registry, immutable read-only descriptor lookup, lease/epoch lifecycle, and manifest-file/hash validation landed<br>
**Created**: 2026-05-14<br>
**Completed**: 2026-06-20<br>
**Parent**: 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md)<br>
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)<br>
**WorkProduct**: 📄[WP — G106 Root1 Slab Advertisement Registry](../../WorkProducts/WP_G106_Root1SlabAdvertisementRegistry.md)

## Objective
Design and implement the first Root1-owned registry for advertised worker slab publications: slab ranges/layers, manifest paths, root ids, epochs, permissions, and lifecycle leases. This should integrate with 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md): Root1's publication directory, resource keys, waitable/mailbox registry, ownership epochs, and lifecycle leases should use one coherent control-plane model.

## Summary
G106 is complete. Root1 now owns an immutable publication registry for worker slab advertisements using logical publication layer ids rather than absolute slab-id range leasing. Publishers lease a logical layer, register a read-only `PublicationDescriptor`, and supply a manifest file whose content hash and root metadata are validated before the descriptor becomes visible to consumers.

Consumers can call `lookupPublication(layerId)` to retrieve enough metadata to mmap/read the publication as read-only without receiving mutable global Listree ownership. Publication visibility is bounded by owner/layer leases and monotonic epochs.

## Implemented Surface
- `PublicationLayerId` logical id type.
- `PublicationPermission::ReadOnly`.
- `PublicationDescriptor` metadata schema.
- `Root1ResourceBroker::leasePublicationLayer(owner, expiresAtTick)`.
- `Root1ResourceBroker::renewPublicationLease(layerId, owner, expiresAtTick)`.
- `Root1ResourceBroker::registerPublication(publication, error)`.
- `Root1ResourceBroker::lookupPublication(layerId)` for consumer metadata lookup.
- `Root1ResourceBroker::retirePublication(layerId, owner)`.
- `Root1ResourceBroker::recoverExpiredPublicationLeases(nowTick)` to withdraw expired publication visibility.

## Design Decision
The MVP uses **Root1-assigned logical publication layer ids**, not absolute slab-id range leasing. This makes worker slab-id collisions impossible at the registry boundary: workers publish metadata into a Root1-owned logical layer, and consumers receive layer/manifest/root metadata for read-only mapping rather than direct mutable global state.

## Manifest Validation
Publication registration now validates both schema-level fields and the backing manifest file:

- required layer/owner/epoch/manifest fields;
- immutable + read-only permission policy;
- root-handle layer consistency;
- manifest file can be opened;
- manifest content hash matches `manifestHash` using the MVP hash form `fnv1a64:<16-hex-digits>`;
- manifest advertises matching layer id, epoch, permission, immutability, root descriptor, root layer, root slab id, root offset, and root generation;
- publisher owns the active layer lease;
- replacement epochs are monotonic.

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
- [x] Add manifest/hash validation before accepting a publication beyond required manifest path/hash fields.
- [x] Add tests for collision rejection, stale epoch rejection, and successful publication lookup.

## Acceptance Criteria
- [x] Root1 can lease/register a worker publication without exposing mutable global state to the worker.
- [x] Slab id collisions are rejected or impossible under the chosen scheme.
- [x] Consumers receive enough metadata to mmap/read a publication as read-only.
- [x] Lifecycle/GC semantics are documented at least at the lease/epoch level.
- [x] Publication registration validates the manifest file/hash rather than only requiring manifest metadata fields.

## Verification — 2026-06-20
- `cmake --build build --target reflect_tests -j$(nproc)` passed.
- Focused G106 publication registry tests passed 4/4:
  - `Root1ResourceBrokerTest.LeasesLogicalPublicationLayerAndPublishesReadOnlyManifest`
  - `Root1ResourceBrokerTest.PublicationRegistryRejectsCollisionsAndStaleEpochs`
  - `Root1ResourceBrokerTest.PublicationRegistryValidatesManifestFileHashAndRootMetadata`
  - `Root1ResourceBrokerTest.ExpiredPublicationLeaseWithdrawsConsumerVisibility`
- Full `Root1ResourceBrokerTest.*` passed 22/22.
- Full `reflect_tests` passed 55/55.
- Affected build targets passed: `reflect_tests`, `edict_tests`, `cpp_agent_tests`.
- Affected Edict Root1/static-image slice passed 26/26: `Root1PrimitiveModuleTest.*:Root1AwaitSchedulerTest.*:StaticDeclarationImageTest.*`.

## Deferred Work
These are deliberately not part of G106 completion:

1. Emit/attach `MailboxPayloadKind::PublishedSlab` descriptors for waiting consumers.
2. Add Edict/module-facing Root1 publication primitives.
3. Integrate process-isolated worker publication handoff from G107-style worker outcomes.
4. Replace the MVP FNV hash with a cryptographic hash if Root1 publication manifests become security-boundary artifacts.

## Dependencies
- Comes after the Root1 broker design in 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md), because G106 reuses its participant identity, lease, and epoch model.
- Follows 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md) for immutable publication safety.
- Builds on 🔗[G096 — Authoritative mmap Session Resume](../G096-AuthoritativeMmapSessionResume/index.md) for session/static-mount resume boundaries.
- Supports G107 process-isolated workers as future publication producers.
