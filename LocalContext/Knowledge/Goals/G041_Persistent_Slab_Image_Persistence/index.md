# [G041] Persistent Slab Image Persistence

## Goal

Extend the `ArenaStore` / `LmdbArenaStore` foundation from metadata-only checkpoints to persisted raw slab images that can be restored into allocator-owned memory after restart.

## Status

- Completed.
- First raw slab-image slice implemented on 2026-03-08 for trivially copyable allocator types.
- LMDB references in this goal are historical; the active forward path is slab-backed persistence without LMDB as a supported dependency.

## Parent / Depends On

- Parent: 🔗[`G040_LMDB_Persistent_Arena_Integration`](../G040_LMDB_Persistent_Arena_Integration/index.md)
- Depends On: completed `ArenaStore` boundary and metadata/slab export-restore hooks. References to `LmdbArenaStore` in this goal reflect the historical implementation state at the time.

## Why This Exists

The persistence boundary had to move beyond metadata-only checkpoints and start persisting actual slab contents. Raw slab-image persistence was the next concrete step toward durable allocator state.

## Scope

- Add slab image save/load operations to the persistence boundary.
- Persist slab contents as opaque values keyed by slab id.
- Restore those slab images into allocator-managed memory without changing Listree layout or pointer/offset semantics.
- Keep metadata-only persistence working as a simpler fallback.

## Required Deliverables

- `ArenaStore` support for slab image save/load.
- `LmdbArenaStore` support for `slab/<id>` persistence.
- Allocator-side export/import hooks for slab images.
- Round-trip tests proving restored slabs match pre-save structural traversal for representative cases.

## Success Criteria

- [x] Slab contents survive save/restart/restore cycles.
- [x] Restored data can be traversed structurally without layout reinterpretation hacks.
- [x] Existing rollback tests continue to pass when persistence is disabled.

## Acceptance Criteria

- Representative persisted allocator state survives save/reset/restore across at least file-backed and LMDB-backed stores without relying on process-local pointer resurrection.
- Structured restore is validated on real tree-backed Listree state, including `AATree<j3::ListreeItem>` traversal through a top-level `ListreeValue` graph.
- The supported versus unsupported `ListreeValue` payload boundary is enforced in code and documented clearly enough that restart safety is unambiguous.
- Persistence demos and focused tests show both successful restore paths and safe rejection of unsupported payloads.

## Initial Verification Plan

- Add focused `reflect_tests` coverage for save/load of representative slab-backed structures.
- Re-run Listree traversal tests against restored state.
- Update persistence demos once slab-image round trips are working.

## 2026-03-08 Raw Slab Image Slice

- Added `ArenaSlabImage` plus `saveSlab(...)` / `loadSlab(...)` support to the `ArenaStore` boundary.
- Extended `MemoryArenaStore`, `FileArenaStore`, and `LmdbArenaStore` with raw slab-image persistence.
- Added allocator-side `exportSlabImages()` and `restoreSlabImages()` hooks for trivially copyable allocator types.
- Added focused round-trip tests in `tst/alloc_tests.cpp` proving slab-backed integer allocations survive save/reset/restore across memory, file, and LMDB stores.
- Extended `tst/demo_arena_metadata_persistence.cpp` to show restored slab-backed integer values after restart-style restore.

## Verified In This Slice

- Real LMDB was exercised through the runtime-loaded `LmdbArenaStore` path, not only the in-memory/file stores.
- Focused slab-image tests now exist for:
  - `MemoryArenaStore`
  - `FileArenaStore`
  - `LmdbArenaStore`
- Full validation remained green after the slice:
  - `reflect_tests`: 20 passing
  - `kanren_tests`: 7 passing
  - `edict_tests`: 51 passing

## Handoff State

- The current raw slab-image implementation is intentionally limited to trivially copyable allocator types.
- `Allocator<int>` is the proof path used by tests and demos because raw byte restore is safe there.
- Non-trivial slab types in the current codebase include structures with `std::string`, `CPtr`, and nested container ownership; blindly restoring them from raw bytes would be unsafe.
- The current persistence boundary is now strong enough that a follow-on agent can work on non-trivial restore strategy without revisiting metadata persistence or LMDB wiring.

## Recommended Next Slice

1. Choose and document a restore strategy for non-trivial slab types:
   - explicit reconstruction from persistence-safe records,
   - type-specific persistence hooks,
   - or a constrained subset of restorable slab types.
2. Prove that strategy first on one representative non-trivial allocator type before attempting full Listree restore.
3. Only then move `G042` into root-offset / VM bootstrap restoration.

## 2026-03-08 Non-Trivial Restore Direction

- Recommended direction: a hybrid persistence model with per-type restore policies rather than extending raw-byte restore to every allocator type.
- Keep raw slab-image save/restore for slab-relocatable types whose persisted bytes contain no process-local ownership.
- Add structured slot-record restore for non-trivial types that own `std::string`, heap buffers, or other runtime-managed state.
- Keep an explicit unsupported bucket for transient/runtime-only values so persistence fails safely instead of reviving invalid process-local pointers.

## 2026-03-08 Structured Restore Slice

- Added a per-type persistence-traits layer so allocator slab persistence can distinguish raw-byte and structured restore paths.
- Added restore-only raw-`SlabId` adoption to `CPtr<T>` so structured rehydration can preserve persisted `inUse` refcounts without double-counting references.
- Added structured slot-record persistence for the first non-trivial Listree-side graph path:
  - `CLL<T>`
  - `AATree<T>`
  - `j3::ListreeValueRef`
  - `j3::ListreeItem`
  - persistence-safe `j3::ListreeValue`
- Added focused file-backed regression coverage proving a `ListreeItem -> CLL<ListreeValueRef> -> ListreeValueRef -> ListreeValue(string)` graph survives save/reset/restore.

## Why This Direction Fits The Codebase

- `Allocator<T>::exportSlabImages()` / `restoreSlabImages()` are currently hard-gated to trivially copyable types, which correctly blocks unsafe byte-wise restore for richer allocator payloads.
- `ListreeItem` and `AATree<T>` both embed `std::string` state, so their object bytes are not restart-safe as raw slab images.
- `ListreeValue` owns external runtime state through `void* data` and frees or deletes that payload based on flags, so raw restart would revive invalid heap/cursor pointers.
- `CPtr<T>` stores only a `SlabId`, which means graph edges can still be represented compactly, but structured restore must avoid normal refcount bumps when rehydrating persisted references.

## Proposed Restore Model

### 1. Add Per-Type Persistence Traits

- Introduce a small persistence-policy layer (for example `ArenaPersistenceTraits<T>`) that classifies allocator types as:
  - `raw_bytes`: existing slab-image restore remains valid,
  - `structured`: restore via explicit slot records,
  - `unsupported`: save/restore rejects the type with a clear error.

### 2. Preserve Persisted Refcounts As Ground Truth

- Keep slab-level `inUse` vectors as the authoritative persisted refcount state.
- Add a restore-only way to write `CPtr`/`SlabId` fields without calling normal `CPtr` constructors or assignments, so structured restore does not double-count references already captured in `inUse`.
- Continue restoring into an empty allocator instance only.

### 3. Use Structured Slot Records For External-Ownership Types

- Persist one record per live slot inside a slab-shaped envelope (`slabIndex`, `count`, `inUse`, plus typed slot payloads).
- First structured targets should be the smallest non-trivial Listree path:
  - `j3::ListreeItem`: persist `name` and `values` slab id.
  - `AATree<j3::ListreeItem>`: persist `name`, `level`, child slab ids, and item slab id.
  - `j3::ListreeValue`: persist `list`/`tree` slab ids, flags, `pinnedCount`, and owned payload bytes only for persistence-safe payload kinds.

### 4. Explicitly Reject Runtime-Only Payloads In The First Safe Slice

- `ListreeValue` entries carrying iterator/cursor ownership should be rejected during persistence.
- Binary payloads should only be restored when they are treated as owned duplicated bytes, not as borrowed process-local addresses.
- This keeps `G041` honest: safe restartable Listree state first, VM/runtime pointer resurrection later only if a persistence-safe representation exists.

## Recommended Proof Order

1. Re-run representative tree-backed/Listree traversal after restore, not only the current `ListreeItem` graph test.
2. Prove the structured path on `AATree<j3::ListreeItem>`-backed tree state, ideally through a real top-level `ListreeValue` object graph.
3. Decide whether more `ListreeValue` payload modes can be made persistence-safe beyond the current duplicated/owned byte path.
4. Only then start the narrow root-anchor experiment in `G042`.

## 2026-03-13 Tree-Backed Restore Validation Slice

- Extended the structured restore validation to the first real top-level tree-backed `ListreeValue` state, not only the earlier single-item graph path.
- Added focused file-backed regression coverage for a restored root object containing:
  - top-level named tree items,
  - a nested tree-backed child value,
  - and a list-valued child under the same root.
- Revalidated traversal by enumerating restored top-level keys and dereferencing nested values through restored `AATree<j3::ListreeItem>` nodes.
- Updated `demo_arena_metadata_persistence` to print the restored top-level tree keys plus nested/list payload values after restart-style restore.

## Current Supported Payload Boundary

- Supported `ListreeValue` payloads for structured persistence currently include:
  - duplicated/owned string-like byte payloads,
  - owned binary byte payloads,
  - tree/list container links represented through persisted slab ids.
- Explicitly unsupported in the current safe slice:
  - iterator/cursor-backed payloads,
  - borrowed external pointers or other non-owned process-local data.
- The boundary is enforced by `ArenaPersistenceTraits<j3::ListreeValue>::exportSlot(...)`, and focused regression coverage now proves both safe rejection and continued acceptance of owned binary payloads.

## Remaining Work After This Slice

- The current supported persistence boundary is complete for the intended safe slice: trivially copyable allocators plus structured Listree-side restore for supported payload modes.
- Future work should be treated as a new persistence-expansion slice only if additional runtime payload classes are deliberately brought into the supported boundary.
