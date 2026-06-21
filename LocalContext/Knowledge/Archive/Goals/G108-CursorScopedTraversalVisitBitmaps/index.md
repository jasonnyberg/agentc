# Goal: G108 — Cursor-Scoped Traversal Visit Bitmaps

|**Status**: COMPLETE
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
- [x] Define `TraversalVisitState` and per-slab `SlabVisitBits` for `seen` and `active` traversal state.
- [x] Rename/clarify semantics: `absolute_visited` → `seen`; `recursive_visited` → `active`.
- [x] Keep traversal state operation-scoped, not permanent cursor state.
- [x] Make the key shape compatible with future layer/arena identity, even if current code uses an implicit default layer.
- [x] Replace `TraversalContext` set usage in `ListreeValue::traverse(...)` and related traversal helpers.
- [x] Add tests for two independent traversals over the same read-only graph and cyclic graphs.
- [x] Benchmark or sanity-check allocation behavior against the current `unordered_set` implementation.

## Summary
G108 delivered per-slab bitmap visit tracking as specified:
- `TraversalVisitState` wraps an `std::unordered_map<uint16_t, PerSlabBits>` where each `PerSlabBits` contains a `std::vector<char>(SLAB_SIZE)` for `seen` (absolute) and `active` (recursive) tracking. Allocation is lazy: bitmaps are created only for slabs actually visited.
- Renamed `absolute_visited` → `seen`, `recursive_visited` → `active` with updated semantics throughout.
- Updated all call sites: `ListreeValue::traverse()`, `copy()`, `toDot()`, `setReadOnly()`, `Cursor::traverse()`.
- Memory: ~2KB per visited slab (dense) vs ~40 bytes per visited node (old unordered_set). For a 1000-node traversal in 3 slabs: ~6KB (bitmap) vs ~40KB (set). Worst-case sparse case (1 node/slab across 4096 slabs) favors the set, but typical traversals are slab-dense.
- No writes to traversed slab-resident node data during visit bookkeeping.
- Bitmap key shape `uint16_t` is compatible with future layered/arena slab identities.
- Benchmark sanity: 10 traversals of a 200-node tree complete in under 10ms.
- Validation: listree_tests 83/83 (76 existing + 7 new), Edict focused slice 75/75.

## Deliverables
- `listree/listree.h` — `TraversalVisitState` class replacing `TraversalContext`
- `listree/listree.cpp` — per-slab bitmap implementation, updated all call sites
- `core/cursor.h`, `core/cursor.cpp` — `TraversalVisitState` in cursor traversal signature
- `listree/traversal_visit_test.cpp` — 7 tests covering independent traversals, cycle handling, fresh-state isolation, clear-reset, dense traversal sanity, and allocation comparison

## Acceptance Criteria
- [x] Traversal visit bookkeeping performs no writes to traversed `ListreeValue`, `ListreeItem`, `ListreeValueRef`, CLL, or AATree slabs.
- [x] Multiple traversal contexts can traverse the same read-only graph concurrently without shared visited-state interference.
- [x] Cycle handling remains correct for breadth-first and depth-first traversal.
- [x] The design can be extended to layered slab ids without invalidating the API.

## Dependencies
Supports 🔗[G105](../G105-ReadOnlyStaticSlabOwnershipModel/index.md), 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md), and future Root1/static-slab traversal. This is lower risk than the ownership/refcount/pinning changes because current traversal state is already external.

## Progress
- 2026-06-06: Landed the first G108 implementation slice. Replaced `TraversalContext` (two `std::unordered_set<SlabId>`) with `TraversalVisitState` using per-slab bitmaps (`std::unordered_map<uint16_t, PerSlabBits>` with `std::vector<char>` of size SLAB_SIZE for `seen` and `active` fields). Updated all call sites: `ListreeValue::traverse()`, `ListreeValue::copy()`, `ListreeValue::toDot()`, `ListreeValue::setReadOnly()`, `Cursor::traverse()`, `Cursor::traverse()` in cursor.cpp. Renamed `absolute_visited` → `seen`, `recursive_visited` → `active`. Added 5 tests: `TwoIndependentTraversalsSameGraph`, `TwoIndependentTraversalsReadOnlyGraph`, `BreadthFirstCycleHandling`, `TraversalVisitStateFreshAfterSeparateUsage`, `ClearResetsState`. Benchmark/deferred allocation behaviour check remains as the last unchecked item. Validation: listree_tests 81/81 (76 existing + 5 new), Edict focused slice 75/75.
