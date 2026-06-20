# WP — G106 Root1 Slab Advertisement Registry

**Goal**: G106 — Root1 Slab Advertisement Registry  
**Status**: Complete  
**Date**: 2026-06-20

## Summary

G106 establishes the first Root1-owned publication registry for immutable worker slab advertisements. The registry uses Root1-assigned logical publication layer ids instead of leasing absolute slab-id ranges. This keeps worker-produced publications out of mutable global Listree state and makes slab-id collisions impossible at the registry boundary.

## Publication Contract

A publisher must:

1. Register as a Root1 participant.
2. Lease a logical publication layer with `leasePublicationLayer(owner, expiresAtTick)`.
3. Produce an immutable read-only publication manifest.
4. Advertise a `PublicationDescriptor` with:
   - `layerId`
   - `owner`
   - `epoch`
   - `manifestPath`
   - `manifestHash`
   - `rootDescriptor`
   - `rootHandle`
   - `permission = ReadOnly`
   - `immutable = true`
5. Keep the publication lease current or allow Root1 to withdraw consumer visibility when the lease expires.

Consumers use `lookupPublication(layerId)` to retrieve enough metadata to mmap/read the publication as read-only without gaining mutable access to global state.

## Manifest Validation

`registerPublication(...)` validates both schema-level and file-backed manifest invariants before storing the publication:

- layer id is non-zero;
- owner is non-zero;
- epoch is non-zero;
- manifest path/hash fields are non-empty;
- publication is immutable and read-only;
- root handle layer is either omitted (`0`) or matches the publication layer;
- manifest file can be opened;
- manifest content hash equals `manifestHash` using the Root1 MVP hash form `fnv1a64:<16-hex-digits>`;
- manifest content advertises matching `layer_id`, `epoch`, `permission`, `immutable`, `root_descriptor`, `root_layer_id`, `root_slab_id`, `root_offset`, and `root_generation` fields;
- owner holds the publication layer lease;
- owner participant is still active;
- publication epoch is monotonic for the layer.

## Lifecycle Semantics

- Publication layers are Root1-assigned logical ids.
- A layer lease is owned by a participant and has an expiry tick.
- `renewPublicationLease(...)` extends an owned layer lease.
- `retirePublication(...)` removes both publication visibility and the layer lease when called by the owning participant.
- `recoverExpiredPublicationLeases(nowTick)` removes expired publication leases and withdraws their consumer-visible publication descriptors.
- Consumers never receive mutable Listree ownership through this registry; they receive read-only publication metadata.

## Test Coverage Matrix

| Test | What it verifies |
|---|---|
| `Root1ResourceBrokerTest.LeasesLogicalPublicationLayerAndPublishesReadOnlyManifest` | Positive publication lifecycle: lease, validate, register, lookup, renew, retire. |
| `Root1ResourceBrokerTest.PublicationRegistryRejectsCollisionsAndStaleEpochs` | Distinct logical layers, wrong-owner rejection, missing metadata rejection, stale epoch rejection, newer epoch acceptance. |
| `Root1ResourceBrokerTest.PublicationRegistryValidatesManifestFileHashAndRootMetadata` | Real manifest open/hash validation plus root metadata mismatch rejection. |
| `Root1ResourceBrokerTest.ExpiredPublicationLeaseWithdrawsConsumerVisibility` | Expired layer leases remove consumer publication visibility and cannot be renewed afterward. |

## Verification

- `cmake --build build --target reflect_tests -j$(nproc)` passed.
- Focused G106 publication registry tests passed 4/4.
- `Root1ResourceBrokerTest.*` passed 22/22.
- Full `reflect_tests` passed 55/55.
- Affected build targets passed: `reflect_tests`, `edict_tests`, `cpp_agent_tests`.
- Affected Edict Root1/static-image slice passed 26/26.

## Deferred Work

Not part of G106 completion:

- Emitting publication descriptors as `MailboxPayloadKind::PublishedSlab` events for waiting consumers.
- Adding Edict/module-facing publication primitives.
- Wiring G107 process-isolated worker outcomes into this registry.
- Replacing the MVP FNV content hash with a cryptographic hash if Root1 publication manifests become security-boundary artifacts.
