# Work Product: LMDB Surface-Area Audit (2026-04-30)

## Purpose
Inventory the remaining LMDB references in the AgentC codebase and classify each one for removal, replacement, archival, or retention as historical context under 🔗[G069](../Goals/G069-RemoveLMDBDependencies/index.md).

## Summary
The audit shows that LMDB is no longer a deep runtime dependency, but it still remains present in four important places:

1. **Build configuration** via `AGENTC_WITH_LMDB` in the root `CMakeLists.txt`.
2. **Core allocator source** via the `LmdbArenaStore` declaration/implementation in `core/alloc.h` and `core/alloc.cpp`.
3. **Validation/demo surface** via LMDB-specific tests and demo branches.
4. **Documentation / HRM memory** where LMDB is still described as an active or recent architecture path.

This means the cleanup is tractable: the remaining active code surface is compact, but it touches foundational allocator files and therefore should be removed in a controlled sequence.

## Primary Evidence
- `./CMakeLists.txt` defines `option(AGENTC_WITH_LMDB "Build with LMDB arena store support" OFF)` and conditionally propagates `AGENTC_WITH_LMDB` to `reflect`.
- `./core/alloc.h` and `./core/alloc.cpp` still declare and implement `LmdbArenaStore` behind `#ifdef AGENTC_WITH_LMDB`.
- `./tests/alloc_tests.cpp` still contains LMDB-only regression coverage guarded by `#ifdef AGENTC_WITH_LMDB`.
- `./demo/demo_arena_metadata_persistence.cpp` still contains an LMDB-specific demo branch.
- `./README.md` still presents LMDB as part of the allocator persistence story.
- Multiple `LocalContext/Knowledge/...` files still describe LMDB as an active design direction rather than legacy context.

## Inventory and Classification

### 1. Build Configuration

#### `CMakeLists.txt`
**Observed surface**
- LMDB option definition.
- comment block describing LMDB as an optional supported backend.
- conditional compile definition propagation.

**Classification**: **Remove / Replace**

**Planned action**
- Remove `AGENTC_WITH_LMDB` option.
- Remove compile-definition propagation.
- Update comments so the supported persistence path is slab-image/file-backed restore only.

---

### 2. Active Source Code

#### `core/alloc.h`
**Observed surface**
- `LmdbArenaStore` class declaration under `#ifdef AGENTC_WITH_LMDB`.

**Classification**: **Remove**

**Planned action**
- Remove the `LmdbArenaStore` type declaration once no active callers remain.

#### `core/alloc.cpp`
**Observed surface**
- `LmdbArenaStore::Impl` implementation.
- LMDB symbol loading via `dlopen` / `dlsym`.
- CRUD operations for metadata, slab images, and root-state records.

**Classification**: **Remove**

**Planned action**
- Remove the LMDB implementation block after confirming that all supported persistence behavior is covered by the non-LMDB store path.

**Notes**
- This is the highest-risk removal area because it shares persistence abstractions with the slab-image path.
- Removal should preserve the generic `ArenaStore` interface and file-backed implementations.

---

### 3. Tests and Demo Surface

#### `tests/alloc_tests.cpp`
**Observed surface**
- LMDB-specific tests for:
  - named checkpoint round-trips,
  - slab-image round-trips,
  - anchored top-level tree restore.

**Classification**: **Replace / Remove**

**Planned action**
- Remove LMDB-only tests.
- Ensure equivalent confidence remains through memory/file-backed persistence tests.
- If needed, strengthen file-backed restore tests before deleting LMDB-specific assertions.

#### `demo/demo_arena_metadata_persistence.cpp`
**Observed surface**
- LMDB branch demonstrating metadata, slab-image, and rollback behavior.

**Classification**: **Remove / Replace**

**Planned action**
- Remove the LMDB demo branch.
- Expand the file-backed slab/root demo if any user-facing persistence story is lost.

---

### 4. Product Documentation

#### `README.md`
**Observed surface**
- Mentions LMDB in the top-level allocator/persistence summary.
- Presents LMDB-oriented persistence as part of the near-future story.

**Classification**: **Replace**

**Planned action**
- Rewrite persistence descriptions around mmap/file-backed slab persistence.
- Remove LMDB from the current-feature summary.

---

### 5. HRM / LocalContext Documentation

#### Files that describe LMDB as active architecture or active capability
- `LocalContext/Knowledge/Facts/J3_AgentC/index.md`
- `LocalContext/Knowledge/Facts/J3_AgentC/K024_J3_SlabAllocator.md`
- `LocalContext/Knowledge/Facts/J3_AgentC/K030_J3_Arena_Persistence.md`
- `LocalContext/Knowledge/Goals/G016-LmdbOptionalBuild/index.md`
- `LocalContext/Knowledge/Goals/G040_LMDB_Persistent_Arena_Integration/index.md`
- `LocalContext/Knowledge/Goals/G041_Persistent_Slab_Image_Persistence/index.md`
- `LocalContext/Knowledge/Goals/G042_Persistent_VM_Root_State_And_Restore_Validation/index.md`
- `LocalContext/Knowledge/Goals/G043-LmdbPickling/index.md`
- `LocalContext/Knowledge/Goals/G044_JSON_Module_Cache/index.md`
- `LocalContext/Knowledge/WorkProducts/AgentCLanguageEnhancements-2026-03-22.md`

**Classification**: **Archive / Revise**

**Planned action**
- Keep historically accurate records.
- Add “legacy / superseded by slab-only persistence direction” framing where needed.
- Update facts that still describe LMDB as part of the current system.

#### Files that can remain mostly unchanged as historical log entries
- `LocalContext/Knowledge/Timeline/2026/03/20/index.md`
- `LocalContext/Knowledge/Timeline/2026/03/22/index.md`
- `LocalContext/Knowledge/Timeline/2026/03/22/2140-2200/index.md`
- `LocalContext/Knowledge/Timeline/2026/03/23/1800-1815/index.md`

**Classification**: **Keep as historical context**

**Planned action**
- Leave timeline entries intact unless they become misleading through surrounding links.

#### Current-session architectural files
- `LocalContext/Knowledge/Goals/G068-ClientAgentSplitEmbeddedVmPersistence/index.md`
- `LocalContext/Knowledge/Goals/G069-RemoveLMDBDependencies/index.md`
- `LocalContext/Dashboard.md`
- `LocalContext/Timeline.md`

**Classification**: **Keep / update as work proceeds**

---

## Removal Order Recommendation

### Step 1 - Documentation Alignment
- Update `README.md`.
- Update LocalContext facts/goals that still present LMDB as active architecture.
- Mark LMDB-specific goals as legacy where appropriate.

### Step 2 - Test/Demo Migration
- Ensure file-backed persistence tests cover the intended confidence level.
- Remove LMDB-only demo/test branches.

### Step 3 - Build Toggle Removal
- Remove `AGENTC_WITH_LMDB` from `CMakeLists.txt`.
- Remove corresponding guarded compilation expectations from docs.

### Step 4 - Core Source Removal
- Delete `LmdbArenaStore` from `core/alloc.h` and `core/alloc.cpp`.
- Verify `ArenaStore`, `MemoryArenaStore`, and `FileArenaStore` still cover supported restart flows.

## Risk Assessment

**Claim**: The LMDB removal appears low-to-moderate risk if performed in the recommended order.
**Evidence**: LMDB is isolated to a compile-time flag, one store implementation, a small number of guarded tests/demos, and documentation. File-backed persistence paths already exist and are actively exercised.
**Confidence**: 85%
**Reasoning**: The active code surface is small and visibly fenced, but allocator persistence is foundational enough that source removal should still be validated carefully.
**Alternatives**: The project could keep LMDB behind the guard indefinitely, but that would preserve architectural ambiguity.
**Gaps**: No full removal has yet been attempted, so there may be hidden references not surfaced by string search (e.g. conceptual dependencies in comments or test assumptions).

## Recommended Next Action
Proceed with the first code/documentation cleanup slice:
1. update `README.md` and key LocalContext persistence facts/goals to mark LMDB as legacy,
2. then remove LMDB-only demo/test branches,
3. then remove the build flag and source implementation.