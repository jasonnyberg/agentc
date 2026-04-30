# [G040] LMDB Persistent Arena Integration

## Status Note
Legacy / superseded as an architecture direction. Keep this goal as historical context for the persistence boundary work, but do not use it as guidance for current implementation planning.

## Goal

Show why an LMDB-backed persistence layer is a strong fit for the slab-based allocator and define an implementation path that preserves current allocator, Listree, and VM semantics with minimal disruption.

## Why This Matters

- The AgentLang design depends on relocatable arena state, integer offsets, and fast checkpoint/restore semantics; LMDB aligns with that model through memory-mapped storage and transactional snapshots.
- Persistence closes one of the largest gaps called out in `j3/LocalContext/Knowledge/WorkProducts/AgentLang.md`: the current codebase has rollback foundations but no durable shared-memory substrate.
- A durable arena enables process restart recovery, zero-copy-ish reloads, reproducible checkpoints, and future shared-state handoff patterns without requiring a rewrite of Listree or Edict execution.

## Expected Benefits

### Functional Benefits

- Durable slab state across process restarts.
- Cheap named checkpoints by persisting allocator watermarks and root offsets.
- Shared substrate for future Cartographer/agent coordination through stable persisted arena metadata.
- Better debugging and reproducibility via persistent snapshots of VM state.

### Architectural Benefits

- Preserves the existing relative-offset model instead of replacing it with object serialization.
- Keeps allocation logic slab-oriented; LMDB acts as a backing store, not a new ownership model.
- Allows incremental adoption: metadata-only persistence first, full slab-page persistence later.

### Operational Benefits

- Minimal runtime impedance: `mmap`-backed pages match the current arena mental model.
- Crash-safe commits via LMDB transactions.
- Natural path to read-only snapshot inspection tools without invasive VM hooks.

## Minimal-Impact Strategy

### Principle

Do not redesign the allocator around LMDB. Instead, add a persistence boundary around the existing slab allocator so current in-memory behavior remains the default execution model.

### Integration Shape

1. Introduce a narrow `ArenaStore` abstraction with operations such as `load_arena`, `save_slab`, `load_slab`, `save_checkpoint`, and `load_checkpoint`.
2. Keep the current slab allocator API and internal pointer/offset semantics unchanged.
3. Add an `LmdbArenaStore` implementation behind the abstraction.
4. Make persistence opt-in at allocator or VM initialization time.

### Data Model

- `meta/current` -> allocator version, slab size, active slab id, current watermark.
- `root/state` -> root Listree offset and top-level VM anchors.
- `slab/<id>` -> raw slab bytes.
- `checkpoint/<name>` -> slab id set, watermark, root offset, timestamp, optional label.

### Low-Risk Phases

#### Phase 1: Metadata Persistence

- Persist allocator metadata and explicit checkpoints only.
- Continue allocating slabs in RAM exactly as today.
- Validate that restore can rebuild allocator state from metadata plus copied slab images.

#### Phase 2: Slab Persistence

- Persist slab contents as opaque LMDB values.
- Restore slabs back into the allocator without changing Listree node layout.
- Add snapshot/load tests around current rollback demos.

#### Phase 3: Mmap-Aware Optimization

- Optionally map persisted slabs directly when alignment and allocator invariants allow.
- Keep this phase isolated so Phases 1 and 2 already deliver value even if direct mapping proves awkward.

## Code Impact Assessment

### Expected Small Changes

- Allocator initialization path: allow optional store attachment.
- Checkpoint/rollback path: add persistence hooks at explicit save boundaries.
- Test harnesses: add persistence round-trip coverage.

### Expected Non-Changes

- No required rewrite of Listree structure.
- No required rewrite of Edict bytecode or VM execution model.
- No pointer swizzling layer if slabs continue using relative offsets.
- No mandatory LMDB dependency for existing tests if the feature is compile-time or runtime optional.

## Success Criteria

- A persisted checkpoint restores allocator watermark, root offsets, and slab contents correctly after process restart.
- Existing rollback tests continue to pass with persistence disabled.
- New persistence tests prove equivalence between pre-save and post-restore traversal of representative Listree/VM state.
- Integration is confined mainly to allocator bootstrap, checkpoint persistence, and new tests.

## Concrete Deliverables

- `ArenaStore` interface and LMDB-backed implementation.
- Allocator/VM bootstrap flag for enabling persistence.
- Round-trip persistence regression tests.
- A small demo showing create -> checkpoint -> restart -> restore -> continue execution.

## Recommended Implementation Plan

### Step 1: Add A Narrow Persistence Boundary

- Introduce an `ArenaStore` interface instead of coupling allocator code directly to LMDB APIs.
- Keep this boundary focused on slab images, allocator metadata, and explicit checkpoints.
- Treat LMDB as a persistence adapter, not as the allocator itself.

### Step 2: Keep Existing Runtime Behavior As Default

- Preserve the current in-memory slab allocator path unchanged when no store is attached.
- Make persistence opt-in through allocator or VM bootstrap configuration.
- Avoid changing Listree node layout, offset encoding, or VM execution semantics in the first slice.

### Step 3: Start With Metadata And Explicit Checkpoints

- Persist only allocator metadata, root offsets, and named checkpoints first.
- Reuse the current rollback/checkpoint concepts already present in the codebase.
- Defer direct LMDB-backed slab residency until a simple round-trip path is proven.

### Step 4: Persist Raw Slab Images Without Reinterpreting Objects

- Store slab bytes as opaque values keyed by slab id.
- Restore those bytes back into allocator-managed memory as-is.
- Continue relying on relative offsets so no pointer swizzling layer is introduced.

### Step 5: Add Validation Before Optimization

- Add save/restore regression tests before attempting direct `mmap`-style optimization.
- Validate structural equivalence by traversing restored Listree and VM state.
- Only pursue direct mapped slabs if alignment, page sizing, and allocator invariants make it clearly worthwhile.

## Suggested Interfaces

The first implementation should stay small and explicit. A minimal interface sketch:

```cpp
struct ArenaCheckpoint {
    uint32_t version;
    uint32_t slab_size;
    uint64_t active_slab_id;
    uint64_t watermark;
    uint64_t root_offset;
};

struct SlabImage {
    uint64_t slab_id;
    std::vector<std::byte> bytes;
};

class ArenaStore {
public:
    virtual ~ArenaStore() = default;

    virtual bool load_current(ArenaCheckpoint& out) = 0;
    virtual bool save_current(const ArenaCheckpoint& checkpoint) = 0;

    virtual bool load_slab(uint64_t slab_id, SlabImage& out) = 0;
    virtual bool save_slab(const SlabImage& slab) = 0;

    virtual bool save_named_checkpoint(std::string_view name,
                                       const ArenaCheckpoint& checkpoint) = 0;
    virtual bool load_named_checkpoint(std::string_view name,
                                       ArenaCheckpoint& out) = 0;
};
```

An LMDB-backed adapter can then implement this interface as `LmdbArenaStore` while allocator code only depends on `ArenaStore`.

## Targeted Code Touch Points

The smallest expected implementation surface is:

- `j3/alloc.cpp` - attach an optional persistence backend and expose save/restore hooks at allocator boundaries.
- Allocator headers or related state definitions - add lightweight metadata structs for checkpoint serialization.
- VM/bootstrap entry points - allow persistence configuration and restore-from-checkpoint startup.
- Existing rollback or speculation tests/demos - extend with persistence round-trip coverage.

This keeps change concentrated in bootstrap and checkpoint plumbing rather than in Listree traversal or opcode execution.

## Smallest First Slice

The lowest-risk first code slice is:

1. Define `ArenaCheckpoint` plus `ArenaStore`.
2. Add a null/default store path so current behavior stays unchanged.
3. Add a simple `LmdbArenaStore` that persists `meta/current` and one named checkpoint record.
4. Teach the allocator to export/import checkpoint metadata without yet moving slab bytes through LMDB.
5. Add one regression test proving that checkpoint metadata survives process restart and can drive allocator reinitialization.

That slice is useful on its own because it establishes schema, wiring, and optional dependency behavior before any raw slab persistence work begins.

## Follow-On Slice After Metadata

Once metadata persistence is stable, the next slice should:

- persist raw slab images under `slab/<id>` keys,
- restore them into allocator-owned memory,
- verify that restored Listree traversal matches pre-save traversal,
- and re-run rollback/speculation demos against restored state.

## Decision Notes

- Prefer compile-time or runtime optional LMDB support so existing tests and users are not forced onto the persistence path.
- Prefer opaque slab-byte storage over clever page-sharing in the first implementation.
- Prefer explicit checkpoint save/load calls over implicit persistence on every allocation.
- Prefer single-writer assumptions initially; multi-process coordination can be layered on later.

## Risks And Mitigations

- LMDB page sizing may not match slab sizing cleanly; mitigate by storing slabs as opaque values before attempting direct page mapping.
- Concurrent writers can complicate future IPC; mitigate by starting with single-writer assumptions and read-only snapshot readers.
- Schema drift can break old snapshots; mitigate with explicit allocator/store versioning in `meta/current`.

## Related Context

- 🔗[`j3/LocalContext/Knowledge/WorkProducts/AgentLang.md`](../../../LocalContext/Knowledge/WorkProducts/AgentLang.md)
- 🔗[`j3/LocalContext/Dashboard.md`](../../../LocalContext/Dashboard.md)

## Status

- Completed as the planning-and-foundation umbrella on 2026-03-07.
- This goal now covers the rationale, interface boundary, metadata slice, and first LMDB adapter slice.
- Remaining implementation work has been split into follow-on goals `G041` and `G042`.

## 2026-03-07 Metadata Slice

- Added `ArenaCheckpointMetadata`, `ArenaStore`, `MemoryArenaStore`, and `FileArenaStore` in `alloc.h` / `alloc.cpp`.
- Added allocator metadata export/restore hooks and a test-only restart helper so checkpoint metadata can be round-tripped through a store and used to resume checkpointed allocation after a simulated restart.
- Added focused regression coverage in `tst/alloc_tests.cpp` for memory-backed and file-backed metadata persistence.
- Added `tst/demo_arena_metadata_persistence.cpp` as the first restart-style metadata persistence demo.

## 2026-03-07 LMDB Adapter Slice

- Added `LmdbArenaStore` in `alloc.h` / `alloc.cpp` behind the existing `ArenaStore` boundary.
- Kept LMDB optional at runtime by dynamically loading `liblmdb` instead of making the whole build depend on LMDB headers or direct linkage semantics.
- Persisted `meta/current` and `checkpoint/<name>` records through LMDB transactions while keeping allocator behavior unchanged when no store is used.
- Added focused LMDB round-trip coverage in `tst/alloc_tests.cpp` and extended `tst/demo_arena_metadata_persistence.cpp` to show both file-backed and LMDB-backed metadata restore.

## Remaining Work After This Slice

- 🔗[`G041_Persistent_Slab_Image_Persistence`](../G041_Persistent_Slab_Image_Persistence/index.md): raw slab-image save/load and allocator-owned restore.
- 🔗[`G042_Persistent_VM_Root_State_And_Restore_Validation`](../G042_Persistent_VM_Root_State_And_Restore_Validation/index.md): root/state persistence, resumed execution, and structural restore validation.
- The dynamic-loading vs optional-header/package integration decision remains open and can be resolved inside `G041` once raw slab persistence is under active implementation.
