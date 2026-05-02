# Goal: G071 - Session-Scoped Allocator Image Persistence

## Goal
Build a fundamental persistence substrate, independent of agent-loop policy and largely independent of Edict surface behavior, that can pickle all allocator slabs into a named session subdirectory, record a durable index mapping allocator names to types and slab files/indexes, and resurrect the entire persisted Listree graph from that session image without a JSON rematerialization trampoline.

## Status
**IN PROGRESS**

## Rationale
The persistence path in `cpp-agent/runtime/persistence/session_state_store.cpp` has now moved past the earlier JSON-rematerialization trampoline, but the broader substrate is still transitional. `SessionStateStore` now saves and restores native rooted state through a manifest/index-driven session-image layer, yet the deeper mmap-backed ownership model, durable/transient contract, and generic allocator-session substrate still need to be completed.

The desired substrate is lower-level and more general:
- session persistence should not be an agent-specific mechanism,
- it should not depend on host-owned transcript/JSON reconstruction,
- it should work for the allocator/Listree substrate directly,
- and it should make per-session slab ownership explicit through a session directory layout.

Current design conception for the authoritative mmap-backed end state:
- allocators should ultimately be able both to attach existing slabs from file-backed storage on restore and to create new slabs file-first by creating/truncating an exact-size slab file and `mmap`ing it as backing instead of always allocating heap slab memory first and serializing later;
- the mmap address is ephemeral process-local state, while the durable source of truth is the session-image bootstrap/catalog plus the file-backed slab artifacts it names;
- `SlabId` should stay in its current form rather than being reformatted, with rehydration happening by reconstructing allocator records so an existing slab index maps to the correct mapped slab base address on each restore;
- slab filenames may follow a convenient handshake convention such as `<session>/<slab_id>` or similar, but that convention is not the sole authority — the bootstrap record must still contain enough allocator/slab ownership metadata to rehydrate allocators correctly;
- each slab should likely carry stable self-identifying metadata for allocator-family membership plus slab id/index, so the first encountered slab of a given allocator type can instantiate that allocator and later slabs of the same type can extend its slab table;
- the bootstrap/catalog layer remains responsible for session coherence: active generation, root anchors, allocator/slab ownership, and any other information needed to reconstruct allocator instances so persisted `SlabId`s resolve correctly after remapping;
- VM lifecycle semantics around persistence should stay explicit: restore should not pretend to be a delayed return from an earlier `save()` call unless full continuations/stacks are also persisted; instead, restore should be modeled as a fresh boot with restored durable state plus an explicit restore/save-completed lifecycle fact that the VM can observe and clear.

This goal captures that lower-level effort. It is a persistence/foundation goal, not merely an agent-loop cleanup task. It underpins the remaining G070/G068 persistence work and also supports the broader direction of de-emphasizing `cpp-agent` in favor of an Edict-native agent-loop architecture.

## Desired End State
A named session has its own directory containing:
- a durable index/manifest describing allocator images,
- slab files grouped by allocator name/type,
- root-anchor metadata sufficient to recover top-level persisted structures,
- and enough metadata to distinguish durable allocator state from transient runtime/import artifacts that must be rehydrated after restore.

At restore time, a fresh process can:
1. open a named session directory,
2. restore allocator ownership from the session index,
3. map/load each allocator slab image into the correct allocator/type boundary,
4. restore root anchors,
5. reconstruct the canonical Listree graph,
6. then separately rehydrate transient runtime/import state.

## Scope
- Define a session image directory contract with named-session isolation.
- Add a durable index/manifest mapping allocator names to type identities, encodings, slab indexes, and slab file locations.
- Support session-owned slab persistence that is designed for mmap-backed ownership rather than JSON rematerialization.
- Restore the full persisted Listree graph from allocator images + root anchors.
- Recast `SessionStateStore` as a thin wrapper over the lower-level session-image substrate rather than the owner of persistence semantics.

## Non-Goals
- Do not redesign agent policy/orchestration here; that belongs to G068/G070.
- Do not couple the persistence substrate to `cpp-agent` specifics.
- Do not persist transient imported handles, sockets, parser state, or live runtime objects as durable allocator state.
- Do not remove `cpp-agent` immediately; instead, make this substrate available so the Edict-native architecture can supersede it cleanly.

## Acceptance Criteria
- [x] A named session persists into its own isolated subdirectory, with no filename collisions across sessions.
- [x] A durable session index exists and maps allocator names to type identities, slab encodings, slab indexes, and file locations.
- [x] The allocator/session image substrate can restore the full persisted Listree graph from native allocator/root data without a normal-path JSON serialize/reset/rematerialize trampoline.
- [x] Focused tests prove that multiple allocators plus root anchors can be resurrected from a named session image into a structurally equivalent Listree graph.
- [ ] The persistence contract clearly separates durable allocator/root state from transient runtime/import artifacts that must be rehydrated after restore.
- [x] `SessionStateStore` or its successor becomes a thin compatibility wrapper over the new substrate rather than a JSON-centered persistence mechanism.
- [x] Durable regression coverage exists for session naming/isolation, manifest/index correctness, and native-root restore.

## Implementation Plan

### Phase 1 - Define the Session Image Contract
- [x] Specify the session directory layout and naming rules.
- [x] Define the session index/manifest schema.
- [x] Enumerate allocator identities that participate in a full Listree restore (`ListreeValue`, `ListreeValueRef`, `CLL<ListreeValueRef>`, `ListreeItem`, `AATree<ListreeItem>`, and any required blob allocator state).
- [x] Define what metadata is durable versus rehydrated.

Phase 1 target: make the substrate concrete enough that implementation does not guess at file layout or restore responsibilities.

Phase 1 evidence: 🔗[WP_SessionImagePersistencePlan_2026-05-01](../../WorkProducts/WP_SessionImagePersistencePlan_2026-05-01.md)

### Phase 2 - Implement Generic Session Image Save/Load
- [x] Add a lower-level session-image store that writes allocator slabs into a named session subdirectory.
- [x] Persist the manifest/index alongside allocator/root metadata.
- [x] Restore allocator populations from that session image using allocator/type-aware loading rather than JSON rematerialization.

Phase 2 target: a generic allocator session-image mechanism exists independently of `cpp-agent` policy.

### Phase 3 - Prepare for Mmap-Backed Slab Ownership
- [x] Shape slab file layout so session-owned slab files can become mmap-backed authoritative storage.
- [x] Decide whether initial implementation uses load/copy semantics with mmap-compatible files first, or direct mmap ownership immediately.
- [x] Document allocator lifecycle requirements for safely adopting mmap-backed session slabs.
- [x] Implement one representative raw-byte allocator attach path that binds mapped slab backing into an allocator slab table instead of decoding that slab into heap-owned bytes first.
- [ ] Add the first file-first allocator slab creation path so one allocator can choose heap-backed or mmap-file-backed slab allocation policy when growing.

Phase 3 target: the implementation is pointed directly at the intended mmap-backed end state instead of another temporary file format.

### Phase 4 - Restore Full Listree Graphs and Roots
- [x] Restore all participating allocators needed for a representative Listree graph.
- [x] Restore root anchors from the session image.
- [x] Prove full graph resurrection through traversal/semantic equivalence tests.

Phase 4 target: a named session image can resurrect representative persisted Listree state directly.

### Phase 5 - Integrate Upward, Then Retire Transitional Paths
- [x] Rework `SessionStateStore` to wrap the new session-image substrate.
- [x] Remove the normal-path JSON serialize/reset/rematerialize trampoline from native-root save/restore.
- [ ] Feed the explicit durable-vs-transient contract back into G070/G068 runtime rehydration work.

Phase 5 target: agent persistence becomes a thin consumer of the substrate, not its special case.

## Related Goals
- Persistence groundwork:
  - 🔗[G041 Persistent Slab Image Persistence](../G041_Persistent_Slab_Image_Persistence/index.md)
  - 🔗[G042 Persistent VM Root State and Restore Validation](../G042_Persistent_VM_Root_State_And_Restore_Validation/index.md)
- Current architecture execution goals:
  - 🔗[G068 Client/Agent Split with Embedded Persistent Edict VM](../G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
  - 🔗[G070 Inverted Loop Surgical Cleanup](../G070-InvertedLoopSurgicalCleanup/index.md)

## Progress Notes

### 2026-05-01
- Identified that the remaining persistence problem is now substrate-level rather than merely host-loop-level: we need a named-session allocator image mechanism that can resurrect full Listree state directly.
- Clarified two independent directions that should now be pursued together without conflating them:
  1. build the generic session-scoped slab/image persistence substrate,
  2. continue de-emphasizing `cpp-agent` in favor of an Edict-native agent-loop architecture.
- Produced 🔗[WP_SessionImagePersistencePlan_2026-05-01](../../WorkProducts/WP_SessionImagePersistencePlan_2026-05-01.md), which now records the proposed session directory contract, manifest responsibilities, allocator set, resurrection workflow, durable-vs-transient boundary, and the current preliminary bootstrap/restore plan for file-backed allocator rehydration.
- Landed the prerequisite layout step in code by changing `SessionStateStore` and `cpp-agent/main.cpp` to use named session subdirectories (`--session`, `AGENTC_SESSION`) so slab/state files are already isolated per session.
- Added durable regression coverage proving named sessions do not clobber one another: `cpp-agent/tests/session_state_store_test.cpp` now validates separate session subdirectories and independent restore.
- Recommended execution order for future work: prioritize G071 until the mmap/session restore substrate is real enough to remove the JSON trampoline, while only making minimal architecture changes above it as needed to stay coherent.
- Implemented the first real session-image substrate slice beneath `SessionStateStore`:
  - added `cpp-agent/runtime/persistence/session_image_store.h`
  - added `cpp-agent/runtime/persistence/session_image_store.cpp`
  - exported reusable arena metadata/slab/root serialization helpers from `core/alloc.h` / `core/alloc.cpp`
- `SessionImageStore` now persists a per-session `manifest.json`, `roots.bin`, and allocator-specific metadata/slab files under `allocators/<name>/...`, giving the slab layout an explicit index-oriented shape that can evolve toward mmap-backed ownership.
- Reworked `SessionStateStore` to consume that substrate instead of the old flat filename-prefix convention. Restore now loads allocator metadata/slab images through the manifest/index and then restores the anchored root from `roots.bin`.
- Removed the normal native-root JSON trampoline by changing `SessionStateStore::saveRoot(...)` to create a native snapshot via allocator checkpoints + `ListreeValue::copy()` + rollback rather than JSON serialize/reset/rematerialize. The saved session image now comes from native slab images plus a root anchor, while the live VM/root state remains intact after save.
- Advanced G071 Phase 3 by making session slab files explicitly mmap-friendly: `SessionImageStore` now writes page-aligned slab files with a durable slab header (`session_image_mmap_v1`), records slab payload offset/size metadata in `manifest.json`, and loads those slab files through `mmap` before decoding them back into allocator images.
- Decided the immediate mmap path should be **load/copy semantics with mmap-compatible slab files first**, not direct live allocator ownership yet. That keeps restore behavior stable while moving the on-disk contract toward the intended authoritative mmap-backed end state.
- Documented the allocator lifecycle requirements for true mmap-backed ownership in 🔗[WP_SessionImagePersistencePlan_2026-05-01](../../WorkProducts/WP_SessionImagePersistencePlan_2026-05-01.md), including attach/detach semantics, checkpoint compatibility, root/manifest commit ordering, partial-write recovery, and the durable/transient boundary.
- Reduced the remaining load/copy gap one step further by exporting view-based arena deserializers in `core/alloc.*` and using them on the mmap load path, so slab payloads no longer have to be copied into temporary `std::string` buffers before decode.
- Captured the current design conception for file-backed allocators in this goal's rationale so a fresh agent can resume the discussion without re-deriving it: mmap address is ephemeral, `SlabId` stays as-is, allocators are rehydrated by reconstructing slab-index→mapped-base associations, slab filenames may be a convenient handshake convention but not the sole authority, the bootstrap/catalog layer remains the structural source of truth for allocator/slab ownership and root recovery, and VM restore should be modeled as an explicit lifecycle event rather than a delayed return from `save()`.
- Extended durable regression coverage in `cpp-agent/tests/session_state_store_test.cpp` so it now validates manifest/index shape, allocator image file presence, named-session isolation, native-root restore through the new substrate, the fact that native snapshot save does not destroy live ambient allocator state, and the presence of mmap-oriented slab-file metadata.
- Began the first true allocator-attach slice in code rather than just in design notes:
  - `SessionImageStore` now writes raw-byte slab images in an attach-friendly mmap format (`session_image_mmap_raw_v1`) with explicit in-use and item-byte regions,
  - `SessionImageStore::loadAllocatorMappedRawSlabs(...)` can now return live mapped raw slab attachments instead of only decoded `ArenaSlabImage` copies,
  - `Allocator<T>::attachMappedRawSlabs(...)` now allows a raw-byte allocator to bind mapped slab backing directly into its slab table,
  - `cpp-agent/tests/session_state_store_test.cpp` now proves a representative `Allocator<uint64_t>` path can save raw slabs, restore metadata, attach mapped slab backing, resolve existing `SlabId`s correctly, and continue reading/writing through that mapped backing without a heap copy of the slab bytes.
- Extended that prototype into the first Listree-participating allocator attach path:
  - `SessionImageStore` now also supports an attach-friendly structured slab format (`session_image_mmap_structured_attach_v1`) that stores the serialized structured slab payload plus reserved mapped item space,
  - `SessionImageStore::saveAllocatorAttachableStructuredSlabs(...)` and `SessionImageStore::loadAllocatorMappedStructuredSlabs(...)` now support mmap-backed attachment for selected structured allocators,
  - `Allocator<T>::attachMappedStructuredSlabs(...)` now restores structured slots directly into mapped slab backing before binding that backing into the allocator slab table,
  - `ArenaMmapStructuredAttachTraits<agentc::ListreeValueRef>` marks `ListreeValueRef` as the first structured/Listree allocator family participating in this path,
  - `cpp-agent/tests/session_state_store_test.cpp` now proves persisted `ListreeValueRef` slabs can be restored through mapped slab attachment, still resolve existing `SlabId`s correctly, and still dereference the referenced `ListreeValue` payload after attachment.
- Recorded the next allocator-lifecycle step explicitly in the plan and rationale: after restore-time attachment, the substrate should learn true file-first slab creation for at least one allocator, with a configurable backing policy that can choose either heap-backed or mmap-file-backed slab allocation as the allocator grows.
- Validation: `ctest -R cpp_agent_tests --output-on-failure` passes.
- Added stronger regression-driving infrastructure while pursuing the allocator work: `cpp-agent/tests/run_cpp_agent_tests_with_timeout.sh` now runs gtests one-by-one under a shell timeout, and `edict/main.cpp` plus `edict/edict_vm.cpp` now expose opt-in startup markers behind `AGENTC_EDICT_TRACE_STARTUP=1` so future Edict bootstrap hangs can be traced without enabling noisy logging by default.

## Next Action
Build on the new raw-byte + `ListreeValueRef` attach paths in two coordinated directions: (a) tighten bootstrap/catalog metadata around allocator-family/slab-self-identification so allocator discovery no longer depends only on the manifest path, and (b) start the first file-first allocator growth path so one allocator can allocate a new slab either on the heap or by creating/truncating and `mmap`ing an exact-size slab file directly.
