# Knowledge: J3 Arena Persistence Boundary

**ID**: LOCAL:K030
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #allocator, #persistence, #lmdb, #checkpoint

## Overview
J3 now has a first persistence boundary around the slab allocator. It currently supports checkpoint metadata export/restore plus three interchangeable store backends: memory-backed, file-backed, and LMDB-backed.

## Implemented Pieces

### `ArenaCheckpointMetadata`
- Captures allocator-level checkpoint metadata such as version, slab sizing, live-slot counts, highest live slot, allocation-log size, and checkpoint stack positions.
- Current restore support is metadata-only; it can revive checkpoint stack state for restart-style validation but does not yet restore raw slab bytes.

### `ArenaStore`
- Narrow persistence boundary for allocator checkpoint data.
- Current working implementations:
  - `MemoryArenaStore`
  - `FileArenaStore`
  - `LmdbArenaStore`

### `LmdbArenaStore`
- Implemented in `alloc.h` / `alloc.cpp`.
- Persists `meta/current` and `checkpoint/<name>` records through real LMDB transactions.
- Keeps LMDB optional by loading `liblmdb` at runtime with `dlopen` instead of depending on LMDB headers at build time.

### `ArenaSlabImage`
- Represents one persisted slab image with slab id, slot usage metadata, and raw bytes.
- Current allocator support can export and restore these images for trivially copyable allocator types.
- The first working raw slab-image path is validated with `Allocator<int>` rather than full Listree/VM object graphs.

## Verified Behavior
- `tst/alloc_tests.cpp` covers metadata round trips through memory, file, and LMDB stores.
- `tst/alloc_tests.cpp` also covers raw slab-image round trips through memory, file, and LMDB stores for slab-backed integer allocations spanning multiple slabs.
- `tst/alloc_tests.cpp` now also covers a file-backed structured round trip for a non-trivial Listree-side graph rooted at `ListreeItem` with a persisted string value.
- `tst/demo_arena_metadata_persistence.cpp` demonstrates restart-style restore for both file-backed and LMDB-backed metadata checkpoints.
- `tst/demo_arena_metadata_persistence.cpp` also demonstrates restored slab-backed values after a file-backed raw slab-image round trip.
- The LMDB-backed path has been exercised against the actual installed `liblmdb.so.0.0.0`, not only mocked or file-backed stand-ins.
- Existing `reflect_tests` and `listree_tests` remain green after the structured restore slice landed.

## Current Limits
- Raw slab-image save/load currently works only for trivially copyable allocator types.
- Root Listree / VM bootstrap anchors are not yet persisted.
- LMDB support is runtime-optional and currently uses dynamic loading instead of an optional compile-time integration path.
- Full Listree / Edict object-graph restore still needs a safe strategy for non-trivial slab types such as `std::string`-bearing nodes.

## Non-Trivial Restore Direction
- The chosen direction is a hybrid model: keep raw slab images for slab-relocatable types, but restore external-ownership types through explicit structured slot records.
- Persisted slab `inUse` vectors remain the source of truth for refcounts during restore.
- Structured restore therefore needs restore-only `CPtr`/`SlabId` adoption helpers that do not increment refcounts while rebuilding object fields.
- The first safe non-trivial slice is now implemented for `CLL<T>`, `AATree<T>`, `ListreeValueRef`, `ListreeItem`, and persistence-safe `ListreeValue` payloads; runtime-only payloads such as iterator-backed `ListreeValue` instances stay unsupported.

## Handoff Guidance
- A new agent should treat metadata persistence and LMDB wiring as complete enough foundations, not active unknowns.
- The next persistence problem is specifically: validate the structured path on tree-backed/top-level Listree state and decide how far to extend payload-mode support before root-anchor persistence.
- `G041` now holds the concrete next-step guidance for that design decision; `G042` should stay blocked until that is resolved.

## Follow-On Goals
- 🔗[`G041_Persistent_Slab_Image_Persistence`](../../../../Knowledge/Goals/G041_Persistent_Slab_Image_Persistence/index.md)
- 🔗[`G042_Persistent_VM_Root_State_And_Restore_Validation`](../../../../Knowledge/Goals/G042_Persistent_VM_Root_State_And_Restore_Validation/index.md)
