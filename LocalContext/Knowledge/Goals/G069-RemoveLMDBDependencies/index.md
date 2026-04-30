# Goal: G069 - Remove LMDB Dependencies

## Goal
Strip active LMDB references, dependencies, and build requirements out of the AgentC codebase so the supported persistence path is exclusively the native memory-mapped slab mechanism.

## Status
**PLANNED**

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
- [ ] The default build no longer requires or links LMDB.
- [ ] Active code paths for persistence point to mmap slab persistence rather than LMDB-backed stores.
- [ ] Documentation, comments, and goals no longer describe LMDB as the intended future architecture.
- [ ] Historical LMDB work is clearly marked as superseded or legacy rather than current direction.
- [ ] Tests and demos continue to validate restart/restore behavior through the slab persistence path.

## Implementation Plan

### Phase 1 - Inventory LMDB Surface Area
- [ ] Enumerate all source files, build files, tests, demos, and docs that reference LMDB.
- [ ] Classify each reference as: remove, replace, archive, or keep as historical context.

### Phase 2 - Build and Dependency Removal
- [ ] Remove LMDB packages, compile flags, and linkage from supported build targets.
- [ ] Ensure the project still builds cleanly with slab persistence as the only supported path.

### Phase 3 - Code Path Simplification
- [ ] Remove or quarantine LMDB-backed arena/persistence implementations from active runtime paths.
- [ ] Replace abstractions that still privilege LMDB as a first-class backend where that no longer matches the architecture.

### Phase 4 - Documentation and Goal Cleanup
- [ ] Update affected goal files, work products, comments, and operational docs to reflect the new persistence direction.
- [ ] Mark LMDB-oriented goals as historical/superseded where appropriate.

### Phase 5 - Validation
- [ ] Run focused build/test validation for slab persistence and restart.
- [ ] Demonstrate that no supported agent runtime path depends on LMDB.

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
Perform an LMDB surface-area audit across source, CMake, tests, demos, and HRM docs, then classify each occurrence for removal, replacement, or archival.