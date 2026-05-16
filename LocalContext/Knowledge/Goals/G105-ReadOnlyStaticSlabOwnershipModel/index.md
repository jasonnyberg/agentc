# Goal: G105 — ReadOnly Static Slab Ownership Model

**Status**: PLANNED  
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
- [ ] Audit allocator `inUse`, `CPtr`, cursor pinning, and `ListreeValue::pinnedCount` assumptions.
- [ ] Incorporate G109's hardened logical ReadOnly mutation surface before relying on OS-level read-only mappings.
- [ ] Define a way to identify static/read-only slab ranges or layers.
- [ ] Implement no-mutate retain/release behavior for static immortal slabs.
- [ ] Define how cursor/path pinning behaves on immortal static nodes.
- [ ] Add tests proving read-only mapped/static nodes can be referenced/read without metadata writes.
- [ ] Document what is immortal, what is private, and what remains future shadow-sidecar work.

## Acceptance Criteria
- [ ] Static/read-only slabs can be mounted and traversed without mutating their `inUse` or node-local pin metadata.
- [ ] Dynamic private slabs keep existing ownership semantics.
- [ ] Tests cover static value retain/release/read traversal and invalid mutation attempts.
- [ ] The model is documented as a prerequisite for Root1/static-core images.

## Dependencies
- Supports 🔗[G103](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md), 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md), and future Root1/micro-VM work.
