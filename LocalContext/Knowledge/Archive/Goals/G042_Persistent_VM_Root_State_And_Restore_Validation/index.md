# [G042] Persistent VM Root State and Restore Validation

## Goal

Build on raw slab persistence by restoring root offsets and VM/bootstrap anchors, then validating that representative Listree and Edict state can resume correctly after restart.

## Status

- Completed.
- 2026-03-13 root-anchor slices completed for representative top-level Listree state, including resumed multi-anchor mutation after restore.
- LMDB references in this goal are historical validation context; the active target architecture is slab-backed restore without LMDB as a supported dependency.

## Parent / Depends On

- Parent: 🔗[`G040_LMDB_Persistent_Arena_Integration`](../G040_LMDB_Persistent_Arena_Integration/index.md)
- Depends On: 🔗[`G041_Persistent_Slab_Image_Persistence`](../G041_Persistent_Slab_Image_Persistence/index.md)

## Why This Exists

Persisted slab bytes are necessary but not sufficient. The system still needs restored root offsets, VM anchors, and structural equivalence validation to make persistence usable for real AgentC state recovery.

## Scope

- Persist and restore root/state anchors such as top-level Listree and VM bootstrap references.
- Validate post-restore equivalence by traversing restored Listree/VM state.
- Re-run rollback/speculation or cognitive-core demos against restored state where appropriate.
- Document the resulting restart workflow and remaining limitations.

## Required Deliverables

- Root/state persistence records in the selected `ArenaStore` backend.
- Restore/bootstrap plumbing for representative allocator/VM state.
- Focused tests proving pre-save and post-restore equivalence for representative state.
- At least one restart-style demo beyond metadata-only checkpoint restoration.

## Success Criteria

- [x] Restored root offsets and VM anchors allow representative state to resume after restart.
- [x] Structural equivalence checks pass across save/restore boundaries.
- [x] User-facing validation artifacts show real resumed state, not only empty metadata checkpoints.

## Acceptance Criteria

- A persisted root-anchor record is sufficient to recover at least one representative top-level VM/Listree state object after allocator restore.
- Focused tests compare pre-save and post-restore structure through traversal or equivalent semantic checks, not only by checking non-null handles.
- At least one restart-style demo shows resumed anchored state doing useful work after restore.
- Remaining unsupported VM/runtime state is documented explicitly so resumed-state claims stay within the validated boundary.

## Current Blocker / Entry Conditions

- `G042` should not start by guessing at root-state persistence in isolation.
- The immediate dependency is a safe non-trivial slab restore path from `G041`.
- Once at least one representative non-trivial allocator type can survive slab-image restore safely, `G042` should begin with a narrow root-anchor experiment rather than full VM-state persistence.

## 2026-03-13 Narrow Root-Anchor Slice

- Extended `ArenaStore` with root-state persistence records so representative stores could save/load root anchors separately from allocator metadata and slab images.
- Added `ArenaRootState` / `ArenaRootAnchor` plus binary encode/decode support in the persistence layer.
- Proved anchored restart on a representative top-level `ListreeValue` tree graph by restoring the saved root anchor after slab-image restore and then traversing the recovered state.
- Added focused regressions for file-backed anchored restore paths and, historically, LMDB-backed validation during that development slice.
- Updated `demo_arena_metadata_persistence` to print the restored root anchor before traversing the restored tree-backed state.

## Verified In This Slice

- Focused anchored-restore regressions now exist for the active `FileArenaStore` path.
- LMDB-backed regressions from the original slice should now be treated as legacy validation history rather than ongoing support requirements.
- Full `./build/tst/reflect_tests` remains green with the new root-anchor coverage (`27` passing).
- `./build/tst/demo_arena_metadata_persistence` now shows a restored root anchor and resumed traversal output.

## Remaining Work After This Slice

- The current validated boundary covers representative root-anchor recovery for persisted Listree state, including multiple anchors and resumed mutation after restore.
- Future persistence work should only extend this goal if broader VM/bootstrap execution state needs to survive restart beyond the current anchored Listree boundary.

## 2026-03-13 Multi-Anchor Resume Slice

- Extended root-state validation from a single representative root anchor to multiple anchors (`root`, `list`, `child`) saved and restored together.
- Added focused reflect coverage proving that restored secondary anchors can be dereferenced directly and used for resumed post-restore work.
- Revalidated resumed usefulness by mutating a restored anchored list after restart and observing the change through the restored root graph.
- Updated `demo_arena_metadata_persistence` so it now prints all restored anchors and demonstrates resumed anchored mutation (`third`) after restore.
