# Goal: G108 — Cursor-Scoped Traversal Visit Bitmaps

**Status**: ACTIVE / IMPLEMENTATION READY  
**Created**: 2026-05-14  
**Parent**: 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md)  
**Related WorkProduct**: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Replace set-based traversal visit tracking with operation-scoped per-slab bitmaps owned by the traversal/Cursor scope, designed so parallel traversals over read-only/shared Listrees never mutate slab-resident nodes for traversal bookkeeping.

## Rationale
Current traversal state is already external to `ListreeValue` flags, but it uses `std::unordered_set<SlabId>` for `absolute_visited` and `recursive_visited`. A bitmap design is more slab-native, deterministic, efficient, and ready for layered mmap slab identities.

G105's audit identified this as the next traversal work needed for true read-only slab safety.

## Implementation Plan
- [ ] Define `TraversalVisitState` and per-slab `SlabVisitBits` for `seen` and `active` traversal state.
- [ ] Rename/clarify semantics: `absolute_visited` → `seen`; `recursive_visited` → `active`.
- [ ] Keep traversal state operation-scoped, not permanent cursor state.
- [ ] Make the key shape compatible with future layer/arena identity, even if current code uses an implicit default layer.
- [ ] Replace `TraversalContext` set usage in `ListreeValue::traverse(...)` and related traversal helpers.
- [ ] Add tests for two independent traversals over the same read-only graph and cyclic graphs.
- [ ] Benchmark or sanity-check allocation behavior against the current `unordered_set` implementation.

## Acceptance Criteria
- [ ] Traversal visit bookkeeping performs no writes to traversed `ListreeValue`, `ListreeItem`, `ListreeValueRef`, CLL, or AATree slabs.
- [ ] Multiple traversal contexts can traverse the same read-only graph concurrently without shared visited-state interference.
- [ ] Cycle handling remains correct for breadth-first and depth-first traversal.
- [ ] The design can be extended to layered slab ids without invalidating the API.

## Dependencies
Supports 🔗[G105](../G105-ReadOnlyStaticSlabOwnershipModel/index.md), 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md), and future Root1/static-slab traversal. This is lower risk than the ownership/refcount/pinning changes because current traversal state is already external.
