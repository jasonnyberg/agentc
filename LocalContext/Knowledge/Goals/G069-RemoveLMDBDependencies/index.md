# Goal: G069 - Remove LMDB Dependencies

## Goal
Strip active LMDB references, dependencies, and build requirements out of the AgentC codebase so the supported persistence path is exclusively the native memory-mapped slab mechanism.

## Status
**COMPLETE**

## Rationale
The project has now chosen a simpler persistence direction: a memory-mapped slab-backed restore path instead of LMDB-backed persistence. Historical LMDB experiments were valuable, but keeping LMDB in the active codebase now creates architectural ambiguity, extra build complexity, and an incentive to preserve a persistence mode that is no longer the intended direction.

This goal exists to make the chosen architecture legible in code: mmap slab persistence is the supported path, and LMDB becomes historical context rather than a runtime/build dependency.

## Scope
- Remove active LMDB dependency wiring from the supported build.
- Remove or isolate LMDB-backed persistence implementations that are no longer part of the desired architecture.
- Remove references that imply LMDB remains the preferred or supported persistence backend.
- Preserve enough historical knowledge to understand prior work without keeping it on the hot path.

## Non-Goals
- Do not erase historical records of LMDB exploration from HRM knowledge.
- Do not remove persistence capabilities altogether.
- Do not regress the slab-image and root-anchor work already proven in prior persistence goals.

## Acceptance Criteria
- [x] The default build no longer requires or links LMDB.
- [x] Active code paths for persistence point to mmap slab persistence rather than LMDB-backed stores.
- [x] Documentation, comments, and goals no longer describe LMDB as the intended future architecture.
- [x] Historical LMDB work is clearly marked as superseded or legacy rather than current direction.
- [x] Tests and demos continue to validate restart/restore behavior through the slab persistence path.

## Implementation Plan

### Phase 1 - Inventory LMDB Surface Area
- [x] Enumerate all source files, build files, tests, demos, and docs that reference LMDB.
- [x] Classify each reference as: remove, replace, archive, or keep as historical context.

Phase 1 evidence: 🔗[WP_LMDB_SurfaceArea_Audit_2026-04-30](../../WorkProducts/WP_LMDB_SurfaceArea_Audit_2026-04-30.md)

### Phase 2 - Build and Dependency Removal
- [x] Remove LMDB packages, compile flags, and linkage from supported build targets.
- [x] Ensure the project still builds cleanly with slab persistence as the only supported path.

Phase 2 evidence: `CMakeLists.txt` no longer defines `AGENTC_WITH_LMDB` or propagates LMDB-specific compile definitions; `reflect`, `reflect_tests`, `demo_arena_metadata_persistence`, and `cpp-agent` rebuild cleanly.

### Phase 3 - Code Path Simplification
- [x] Remove or quarantine LMDB-backed arena/persistence implementations from active runtime paths.
- [x] Replace abstractions that still privilege LMDB as a first-class backend where that no longer matches the architecture.

Phase 3 evidence: `core/alloc.h` / `core/alloc.cpp` no longer define `LmdbArenaStore`; LMDB-only branches were removed from `tests/alloc_tests.cpp` and `demo/demo_arena_metadata_persistence.cpp`.

### Phase 4 - Documentation and Goal Cleanup
- [x] Update affected goal files, work products, comments, and operational docs to reflect the new persistence direction.
- [x] Mark LMDB-oriented goals as historical/superseded where appropriate.

Phase 4 evidence: `README.md`, `LocalContext/Knowledge/Facts/J3_AgentC/K030_J3_Arena_Persistence.md`, `LocalContext/Knowledge/Facts/J3_AgentC/K024_J3_SlabAllocator.md`, `LocalContext/Knowledge/Facts/J3_AgentC/index.md`, `LocalContext/Knowledge/Goals/G040_LMDB_Persistent_Arena_Integration/index.md`, `LocalContext/Knowledge/Goals/G041_Persistent_Slab_Image_Persistence/index.md`, `LocalContext/Knowledge/Goals/G042_Persistent_VM_Root_State_And_Restore_Validation/index.md`

### Phase 5 - Validation
- [x] Run focused build/test validation for slab persistence and restart.
- [x] Demonstrate that no supported agent runtime path depends on LMDB.

Phase 5 evidence: `cmake .. && make reflect reflect_tests demo_arena_metadata_persistence cpp-agent`, focused `reflect_tests` coverage for memory/file-backed slab and anchored restore paths, and repo search over active code/build paths showing no remaining `LMDB` / `LmdbArenaStore` / `AGENTC_WITH_LMDB` references.

## Related Goals
- Historical persistence path:
  - 🔗[G040 LMDB Persistent Arena Integration](../G040_LMDB_Persistent_Arena_Integration/index.md)
  - 🔗[G041 Persistent Slab Image Persistence](../G041_Persistent_Slab_Image_Persistence/index.md)
  - 🔗[G042 Persistent VM Root State and Restore Validation](../G042_Persistent_VM_Root_State_And_Restore_Validation/index.md)
  - 🔗[G043 LMDB Pickling](../G043-LmdbPickling/index.md)
- New target architecture:
  - 🔗[G068 Client/Agent Split with Embedded Persistent Edict VM](../G068-ClientAgentSplitEmbeddedVmPersistence/index.md)

## Risks and Mitigations
- **Risk**: Removing LMDB support inadvertently regresses persistence abstractions that slab restore still depends on.
  - **Mitigation**: inventory first, then remove incrementally with focused validation.
- **Risk**: Historical persistence goals become misleading after code removal.
  - **Mitigation**: keep HRM records, but mark them as legacy/superseded.

## Next Action
Fold the slab-only persistence direction into G068 implementation planning: define embedded-agent startup/shutdown checkpoint semantics and VM restore hooks for the reconnectable background agent architecture.