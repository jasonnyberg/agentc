# Goal: G105 — ReadOnly Static Slab Ownership Model

**Status**: ACTIVE / STATIC OWNERSHIP PROBE
**Created**: 2026-05-14  
**Parent**: 🔗[G096 — Authoritative mmap Session Resume](../G096-AuthoritativeMmapSessionResume/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Define and implement the first ownership/lifetime model for read-only static mmap slabs, starting with immortal static slabs for the lifetime of a mounted image and deferring shadow refcount/pin sidecars unless needed.

## Rationale
Current `CPtr`/allocator behavior mutates slab refcounts, and `ListreeValue` contains mutable `pinnedCount`. Truly read-only mmap slabs cannot accept those writes. The full-send architecture needs a safe model for static core/import slabs and immutable published result slabs, distinct from the mutable coordination/mailbox slabs governed by 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md).

Related audit: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md) inventories slab-resident mutable operational state and separates traversal bitmap work (🔗[G108](../G108-CursorScopedTraversalVisitBitmaps/index.md)) from read-only mutation hardening (🔗[G109](../G109-ListreeReadOnlyMutationSurfaceHardening/index.md)).

## Preferred First Model
Treat static/core slabs as **immortal for their image/lease lifetime**:
- retaining/releasing static values does not mutate read-only slab metadata;
- static nodes are not individually freed;
- lifecycle is governed by image manifests, Root1 leases, and process mappings;
- mutable ownership/pinning remains in private dynamic slabs or explicit G110 coordination sidecars, not in read-only static nodes.

Alternatives to keep in view:
- process-local shadow refcount/pin sidecars;
- borrowed/static handle APIs;
- copy-on-write promotion into private dynamic slabs.

## Implementation Plan
- [x] Audit allocator `inUse`, `CPtr`, cursor pinning, and `ListreeValue::pinnedCount` assumptions. Evidence captured in 🔗[WP — Static Slab Ownership Audit](../../WorkProducts/WP-StaticSlabOwnershipAudit-2026-05-19/index.md).
- [x] Incorporate G109's hardened logical ReadOnly mutation surface before relying on OS-level read-only mappings; G109 is complete for public VM/Cursor mutation surfaces, but this audit records why logical `ReadOnly` is not enough for OS-read-only slabs.
- [x] Define a first way to identify static/read-only slab ranges or layers: `Allocator<T>::markSlabStaticImmortal(...)` plus `slabIsStaticImmortal(...)` marks process-local immortal static slab indices for the first probe.
- [x] Implement first no-mutate retain/release behavior for static immortal slabs: `tryRetain`, `modrefs`, and `deallocate` skip `inUse` mutation/destruction for static-immortal slab ids.
- [x] Define first cursor/path pinning behavior on immortal static nodes: `ListreeValue::pin()` / `unpin()` no-op when the value resides in a static-immortal `ListreeValue` slab.
- [x] Add tests proving static marked nodes can be referenced/read/traversed without metadata writes to `inUse` or `pinnedCount` in the first process-local static probe.
- [x] Add first tiny allocator-mounted static dictionary lookup test across the active Listree allocator family (`ListreeValue`, `ListreeValueRef`, `CLL`, `ListreeItem`, `AATree`) by marking active slabs static-immortal, dropping ordinary handles, re-adopting the static root id, and proving direct named lookup still works without refcount/live-slot churn.
- [x] Add explicit process-local static mark clearing APIs (`clearStaticImmortalMarks`, unmark helpers, and mark-count inspection) plus a focused test proving clearing marks restores ordinary release/refcount mutation for a mounted static dictionary handle.
- [x] Add a first `ListreeStaticMountLease` wrapper that marks active Listree-family slabs static-immortal and releases exactly its own marks on destruction/release, backed by allocator mark reference counts so external static owners are not cleared accidentally.
- [x] Add first `ListreeStaticMountRegistry` with explicit logical mount ids, root id lookup, root handle access, active status, mount metadata, section descriptors, Root1-compatible resource descriptor, and unmount release through `ListreeStaticMountLease`.
- [x] Add OS read-only backing test: synthesize a mock file-backed static slab, `mmap` it with `PROT_READ` only, and prove a Listree value can be read from it without crashing, enforcing the OS-level constraint against `inUse` and `pinnedCount` mutation.
- [x] Document what is immortal, what is private, and what remains future shadow-sidecar work in the first audit/work product.

## Audit Findings — 2026-05-19

The first focused audit is captured in 🔗[WP — Static Slab Ownership Audit](../../WorkProducts/WP-StaticSlabOwnershipAudit-2026-05-19/index.md). Key findings:

- Current `Allocator<T>` stores live refcounts in slab-resident `inUse`; `CPtr` copy/destruction, `Allocator::tryRetain`, `Allocator::modrefs`, and `Allocator::deallocate` mutate that array during normal reference use.
- Current `MmapFile` allocator slabs are writable persistence slabs, not static read-only slabs: both create and reattach paths map `[inUse][items]` with `PROT_READ | PROT_WRITE`.
- `Cursor` navigation writes `ListreeValue::pinnedCount` through `pinPath()` / `unpinPath()` even for read-only logical values.
- Current structured persistence serializes mutable operational fields such as `pinnedCount` and strips logical `ReadOnly` on restore, which is correct for mutable session restore but not sufficient for static core images.
- G109's logical `ReadOnly` guards are necessary but insufficient for true OS-read-only static slabs because allocator refcounts, cursor pinning, and execution frame state can still write outside public mutation APIs.

## Current Decision

The first G105 ownership model remains **immortal static slabs for image/lease lifetime**:

- static nodes are not individually freed;
- retain/release of static ids must not mutate static slab `inUse`;
- static cursor pin/unpin must be no-op or process-local sidecar-backed;
- mutable VM stacks, activation frames, native handles, job records, eventfds/pidfds, provider handles, and Cartographer sidecars stay private/process-local or Root1-owned;
- G103 static declaration images contain metadata, not authority.

## First Static Ownership Probe — 2026-05-19

The first probe implements process-local static-immortal slab metadata without claiming true OS-read-only mmap support yet:

- `Allocator<T>::markSlabStaticImmortal(uint16_t slabIndex)` marks an already-live slab as static/immortal for the process.
- `Allocator<T>::markSlotStaticImmortal(SlabId)` marks exact live slots static/immortal, allowing mounted declaration images to avoid making unrelated dynamic values in the same slab immortal.
- `Allocator<T>::slabIsStaticImmortal(uint16_t slabIndex)` and `Allocator<T>::slotIsStaticImmortal(SlabId)` expose the process-local static predicates.
- For static-immortal slabs/slots, `tryRetain(...)`, `modrefs(...)`, and `deallocate(...)` avoid mutating slab-resident `inUse` and do not destruct individual static objects.
- `ListreeValue::pin()` and `ListreeValue::unpin()` detect static-immortal `ListreeValue` residency and no-op instead of mutating `pinnedCount`.
- `StaticSlabOwnershipTest.StaticImmortalSlabRetainReleaseAndCursorPinAreNoMutate` proves copied `CPtr` references, direct pin/unpin, and basic read/traversal of a static-marked Listree value leave both allocator refs and `pinnedCount` unchanged.
- `StaticSlabOwnershipTest.StaticMountedDictionaryLookupSurvivesHandleDropWithoutMutatingStaticSlabs` is the first tiny allocator-mounted static dictionary lookup probe. It freezes a small named-object tree, marks every active Listree-family slab static-immortal, lets ordinary handles drop, re-adopts the root `SlabId`, and performs direct named lookup while asserting root refs and aggregate live-slot counts remain stable.
- `StaticSlabOwnershipTest.StaticMountClearRestoresNormalReleaseSemantics` proves that clearing process-local static marks during a mounted dictionary handle lifetime re-enables normal `CPtr` release/refcount mutation on drop.
- `ListreeStaticMountLease` is the first explicit static mount/unmount wrapper for the Listree allocator family. It records slab marks made for the mounted image lifetime and releases them in reverse allocator order. Allocator static slab/slot marks are now reference-counted, so lease release does not clear independently-held static ownership marks. `StaticSlabOwnershipTest.StaticMountLeaseReleasesExactMarksWithoutClearingExternalOwners` covers this exact-mark behavior.
- `StaticSlabOwnershipTest.StaticMountCanBeBackedByOSReadOnlyMmap` proves that when a slab image is mapped `PROT_READ` via the OS, reading the nodes operates safely. The test asserts the node is readable, implicitly confirming no hidden initial metadata writes occur, and comments note that writing to `inUse` or node properties would correctly OS-segfault.
- `StaticSlabOwnershipTest.StaticMountRegistryExposesLogicalMountIdsAndRoots` covers the first logical image/root registry seam: mounting an active static root returns a process-local mount id, root lookup works after ordinary handles drop, and unmount clears the lease-owned static marks plus registry access.
- `StaticDeclarationImageTest.RegistryBackedMountExposesLogicalMountIdAndRootMetadata` wires the registry seam into G103 declaration-image metadata: `mountDeclarationImageReadOnly(image, registry)` now returns `mountId` and `rootId`, stores image id, manifest hash, root descriptor, section descriptor, provenance, Root1-compatible resource descriptor, and first section descriptors/ranges, validates root lookup after ordinary handles drop, and verifies registry unmount clears lease-owned static ownership.
- G103 now uses the slot-level seam in `mountDeclarationImageReadOnly(...)` so declaration images can be logically frozen and marked static-immortal without marking whole mixed-use dynamic slabs.

This is still a **probe**, not final static slab mounting: static slabs are marked after normal allocation, and the backing memory is not yet protected by OS `PROT_READ`. It is nevertheless the first implementation seam G103 needs before declaration images can become true read-only slab mounts.

## Recommended Next Implementation Slice

Continue toward true G103/G105 convergence by replacing the test-only dummy mmap file with an actual file-backed G103 static declaration image and connecting registry metadata to Root1-advertised mount/resource handles.

## Acceptance Criteria
- [ ] Static/read-only slabs can be mounted and traversed without mutating their `inUse` or node-local pin metadata.
- [ ] Dynamic private slabs keep existing ownership semantics.
- [ ] Tests cover static value retain/release/read traversal and invalid mutation attempts.
- [ ] The model is documented as a prerequisite for Root1/static-core images.

## Dependencies
- Supports 🔗[G103](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md), 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md), and future Root1/micro-VM work.
