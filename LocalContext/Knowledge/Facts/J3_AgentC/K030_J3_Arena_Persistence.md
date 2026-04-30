# Knowledge: J3 Arena Persistence Boundary

**ID**: LOCAL:K030
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #allocator, #persistence, #checkpoint, #slab

## Overview
J3 now has a first persistence boundary around the slab allocator. The active supported path is slab-oriented persistence through memory-backed and file-backed stores, with current architecture work standardizing on slab-backed restore rather than LMDB.

## Legacy Note
LMDB-backed persistence existed as an earlier experimental adapter. It should now be treated as historical context rather than the intended forward architecture.

## Implemented Pieces

### `ArenaCheckpointMetadata`
- Captures allocator-level checkpoint metadata such as version, slab sizing, live-slot counts, highest live slot, allocation-log size, and checkpoint stack positions.
- Current restore support is metadata-only; it can revive checkpoint stack state for restart-style validation but does not yet restore raw slab bytes.

### `ArenaStore`
- Narrow persistence boundary for allocator checkpoint data.
- Current supported implementations:
  - `MemoryArenaStore`
  - `FileArenaStore`
- Historical implementation:
  - `LmdbArenaStore` (legacy / being removed)

### `ArenaSlabImage`
- Represents one persisted slab image with slab id, slot usage metadata, and raw bytes.
- Current allocator support can export and restore these images for trivially copyable allocator types.
- The first working raw slab-image path is validated with `Allocator<int>` rather than full Listree/VM object graphs.

## Verified Behavior
- `tst/alloc_tests.cpp` covers metadata round trips through memory and file-backed stores.
- `tst/alloc_tests.cpp` also covers raw slab-image round trips through memory and file-backed stores for slab-backed integer allocations spanning multiple slabs.
- `tst/alloc_tests.cpp` now also covers a file-backed structured round trip for a non-trivial Listree-side graph rooted at `ListreeItem` with a persisted string value.
- `tst/demo_arena_metadata_persistence.cpp` demonstrates restart-style restore through the file-backed checkpoint/slab path.
- `tst/demo_arena_metadata_persistence.cpp` also demonstrates restored slab-backed values after a file-backed raw slab-image round trip.
- Existing `reflect_tests` and `listree_tests` remain green after the structured restore slice landed.

## Current Limits
- Raw slab-image save/load currently works only for trivially copyable allocator types.
- Full Listree / Edict object-graph restore still needs a safe strategy for non-trivial slab types such as `std::string`-bearing nodes.
- Runtime-owned artifacts still need explicit rehydration boundaries during restore.

## Non-Trivial Restore Direction
- The chosen direction is a hybrid model: keep raw slab images for slab-relocatable types, but restore external-ownership types through explicit structured slot records.
- Persisted slab `inUse` vectors remain the source of truth for refcounts during restore.
- Structured restore therefore needs restore-only `CPtr`/`SlabId` adoption helpers that do not increment refcounts while rebuilding object fields.
- The first safe non-trivial slice is now implemented for `CLL<T>`, `AATree<T>`, `ListreeValueRef`, `ListreeItem`, and persistence-safe `ListreeValue` payloads; runtime-only payloads such as iterator-backed `ListreeValue` instances stay unsupported.

## Handoff Guidance
- A new agent should treat the slab/file-backed persistence boundary as the active foundation.
- LMDB references in older goals and notes are historical and should not guide new implementation work.
- The next persistence problem is integrating structured slab restore with the embedded-agent lifecycle and explicit rehydration of transient runtime artifacts.

## Follow-On Goals
- 🔗[`G041_Persistent_Slab_Image_Persistence`](../../../../Knowledge/Goals/G041_Persistent_Slab_Image_Persistence/index.md)
- 🔗[`G042_Persistent_VM_Root_State_And_Restore_Validation`](../../../../Knowledge/Goals/G042_Persistent_VM_Root_State_And_Restore_Validation/index.md)
