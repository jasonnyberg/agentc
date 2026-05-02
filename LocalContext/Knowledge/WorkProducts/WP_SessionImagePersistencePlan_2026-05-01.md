# WP: Session Image Persistence Plan (2026-05-01)

## Purpose
Describe the lower-level persistence mechanism now needed beneath the agent architecture work:
- named-session slab/image storage,
- allocator/type/slab indexing,
- whole-Listree resurrection,
- and explicit separation between durable allocator state and transient runtime/import artifacts.

## Why This Is Separate From The Agent Loop
This mechanism should not be owned by `cpp-agent` or by the agent loop itself.
It should be a generic substrate that any embedded Edict/Listree host can use.

The agent-loop architecture can then depend on it, but not define it.

## Proposed Session Directory Contract

```text
<state-root>/
  <session-name>/
    manifest.json
    roots.json              # or root-state.bin if we keep binary root metadata
    allocators/
      ListreeValue/
        meta.json
        slab.0000
        slab.0001
      ListreeValueRef/
        meta.json
        slab.0000
      ListNode/
        meta.json
        slab.0000
      ListreeItem/
        meta.json
        slab.0000
      ListreeTree/
        meta.json
        slab.0000
      BlobAllocator/
        meta.json
        slab.0000
```

The exact metadata split can vary, but the contract must preserve:
- named session isolation,
- allocator identity,
- slab index to file mapping,
- root-anchor recovery,
- versioned schema migration room.

## Minimum Manifest Responsibilities
The manifest/index must map:
- allocator logical name
- allocator type identity
- item size / encoding mode
- slab indexes present for that allocator
- slab file names
- schema version
- root-state location
- session metadata version

Example shape:

```json
{
  "version": 1,
  "session": "default",
  "roots_file": "roots.json",
  "allocators": [
    {
      "name": "ListreeValue",
      "type": "agentc::ListreeValue",
      "encoding": "structured",
      "slabs": [
        {"index": 0, "file": "allocators/ListreeValue/slab.0000"}
      ]
    }
  ]
}
```

## Durable vs Transient Boundary

### Durable
- allocator slab contents for supported persistence-safe allocator types
- root anchors / root state
- session metadata needed to restore allocator populations
- declarative runtime configuration metadata that should survive restart

### Transient / Rehydrated
- imported FFI bindings
- dynamic library handles
- runtime handles
- sockets
- parser buffers
- network/session objects
- any other process-local execution state

## Resurrection Workflow
1. Open session directory.
2. Read manifest/index.
3. Restore allocator metadata and slab populations allocator-by-allocator.
4. Restore root anchors.
5. Reconstruct top-level Listree graph from restored allocators.
6. Create embedded VM around restored root.
7. Rehydrate transient runtime/import artifacts.

## Design Constraint For The Next Slice
The new substrate should be pointed at mmap-backed slab ownership from the start.
That does not necessarily mean the first code slice must immediately adopt live mmap ownership, but it does mean:
- file naming,
- manifest structure,
- allocator identity mapping,
- and restore contract
should all support that destination directly.

Current implementation note:
- The slab files are now mmap-friendly (`session_image_mmap_v1`) and the load path already mmaps those files before decode.
- The current restore model is still **load/copy**, not authoritative live mmap ownership.
- That is intentional: it stabilizes the file contract first while keeping allocator/runtime behavior predictable during the transition.

## Allocator Lifecycle Requirements For True Mmap-Backed Session Slabs
To move from load/copy restore to authoritative mmap ownership safely, the allocator/runtime contract needs to satisfy all of the following:

1. **Stable slab-file identity**
   - A mapped slab file must correspond to one allocator, one slab index, and one durable format version.
   - Manifest metadata must stay authoritative for allocator/type identity and payload placement.

2. **No hidden process-local ownership inside mapped bytes**
   - Only persistence-safe slab payloads may become authoritative mmap-backed state.
   - Runtime-only artifacts (FFI handles, sockets, parser state, live cursors, imported library handles) must remain outside mapped authoritative slabs or be represented declaratively.

3. **Allocator attach/detach semantics**
   - Allocators need an explicit way to attach to mapped slab backing on startup/restore and detach/flush on shutdown.
   - That attach path must not assume that ordinary heap-owned slab allocation is the only lifetime model.

4. **Read/write visibility rules**
   - The project must decide whether mapped slabs are initially attached read-only then promoted, or attached writable immediately.
   - Save/flush semantics must be explicit enough that a host crash does not leave manifest/root metadata claiming a state that slab files never durably reached.

5. **Checkpoint / rollback compatibility**
   - The current allocator checkpoint model assumes append-only or rollback-managed live memory.
   - Direct mmap ownership must define how checkpoints interact with mapped slabs: append-only growth, copy-on-write, shadow slabs, or another mechanism.

6. **Root/manifest commit ordering**
   - Slab durability and root-anchor durability must be ordered so a restart never points at a root state whose slab population is not durably present.
   - In practice that means treating manifest/root updates as a commit record layered on top of slab-file writes.

7. **Recovery behavior for partial writes**
   - The session image contract needs a clear rule for torn/incomplete slab writes, mismatched manifest entries, or stale roots.
   - Versioned headers and manifest validation should reject ambiguous mixed-generation state rather than guessing.

8. **Blob allocator participation**
   - Large payload storage (`BlobAllocator`) must eventually be brought into the same durable session-image contract if it is to become authoritative mmap-backed state rather than merely a transient reconstruction detail.

9. **Compatibility window**
   - During migration, the substrate should tolerate both legacy slab-image files and mmap-oriented slab files long enough to avoid breaking existing persisted sessions unnecessarily.

10. **VM/runtime rehydration remains separate**
   - Even once slab ownership is mmap-backed, startup must still rehydrate transient runtime/import state explicitly after logical/root state is attached.
   - Mmap authority is about durable logical state, not process-local execution handles.

## Preliminary Bootstrap / Restore Plan
This is a preliminary plan rather than a frozen format. The aim is to begin the journey toward authoritative file-backed allocator ownership without prematurely locking the final binary metadata design.

### Guiding Principles
- Keep `SlabId` unchanged.
- Treat mmap addresses as ephemeral process-local attachment results, not durable truth.
- Rehydrate allocator meaning by reconstructing allocator records and slab-index-to-base-address mappings.
- Allow slab filename conventions such as `<session>/<slab_id>` as a convenient handshake, but do not make filename shape the only authority.
- Keep VM save/restore lifecycle semantics explicit: restore is a fresh boot with restored durable state plus an explicit lifecycle fact, not a delayed return from `save()`.

### Proposed Layering
1. **Bootstrap record**
   - Minimal, stable, versioned session metadata.
   - Sufficient to identify the active persisted session image/generation and discover the root set of slabs that should be considered part of the session.
   - Carries enough structure to rehydrate allocators and roots even if filename conventions are unavailable or ambiguous.

2. **Self-identifying slab records**
   - Each slab should carry a small stable header describing at least allocator-family/type membership, slab id/index, and slab payload format/version.
   - The first encountered slab for a given allocator family can instantiate the allocator; later slabs of the same family extend its slab table.

3. **Allocator reconstruction layer**
   - Restore walks the slab set, groups slabs by allocator family, attaches mapped slab backing, and rebuilds allocator-local slab-index lookup tables.
   - After allocator reconstruction, existing `SlabId` values become meaningful again without changing their in-memory representation.

4. **Root/lifecycle reconstruction layer**
   - Restore then reattaches root anchors and emits an explicit VM-visible lifecycle fact indicating that a restore occurred.
   - Transient runtime/import state is rehydrated afterward and remains outside the durable slab authority.

### Preliminary Restore Flow
1. Open session directory.
2. Read the bootstrap metadata.
3. Discover the slab set belonging to the active image/generation.
4. For each discovered slab:
   - mmap the slab file,
   - inspect the slab header,
   - instantiate the allocator for that slab's family if needed,
   - attach the slab's mapped base address under its slab id/index.
5. Once all slabs are attached, restore root anchors.
6. Create or resume the embedded VM around the restored root state.
7. Inject explicit restore/save-completed lifecycle metadata for the VM to observe.
8. Rehydrate transient runtime/import state.

### Preliminary Save Flow
1. Reach a save-safe point / explicit save request.
2. Materialize or update any slab-local headers needed for durable identity and compatibility checks.
3. Flush slab-backed durable state.
4. Write or update bootstrap/root metadata only after slab durability is sufficient for a restart to trust it.
5. Record any explicit save-completed / pending-restore lifecycle fact needed on next boot.

### Early Implementation Sequence
1. Define the minimum bootstrap responsibilities separately from the current manifest so the eventual authority boundary is explicit even if the first implementation still coexists with transitional metadata.
2. Introduce or formalize slab self-identification fields needed for allocator-family discovery and slab-id recovery.
3. Implement one representative allocator attach path that binds a mapped slab directly into an allocator's slab table instead of decoding it into heap-owned state.
4. Prove that a restored allocator can resolve existing `SlabId`s correctly through reconstructed slab-index mappings.
5. Add explicit VM lifecycle metadata for restore/save-completed handling.
6. Start the first true file-first allocator experiment: teach one allocator to create a new slab by creating/truncating a slab file to its exact computed size and `mmap`ing that file as the slab's backing from birth rather than allocating heap memory first.
7. Only after those slices are proven, decide how much authority remains in the manifest versus the new bootstrap metadata.

### File-First Allocation Direction
The next architectural step after restore-time attachment is allocator-time file-backed slab creation.

The intended model is:
- allocator decides a new slab is needed,
- allocator computes the exact slab file size from known geometry (header, in-use/refcount region, item region, alignment/padding),
- allocator creates or truncates the slab file to that exact size,
- allocator `mmap`s the file,
- allocator installs that mapped region as the slab backing immediately.

This should eventually allow allocator storage policy to become configurable per allocator/session, with at least two backing modes:
- **heap-backed slabs** for compatibility, tests, and transitional paths,
- **mmap-file-backed slabs** for authoritative persistent backing.

That dual-mode direction preserves incremental adoption: the allocator API can grow a backing-policy option without requiring the whole runtime to flip to file-first slab creation in one step.

### Open Design Questions To Preserve
- How minimal can the bootstrap record be while still being authoritative enough for recovery?
- Which metadata belongs centrally in bootstrap/catalog versus redundantly in each slab header?
- What is the right stable allocator-family identity token?
- How should active-generation / partial-write recovery be represented?
- How should root anchors be encoded once the bootstrap path is no longer JSON/manifest-centric?
- When does filename convention become merely advisory versus actively used for recovery tooling?

## Relationship To `cpp-agent`
`cpp-agent` should become a consumer of this substrate, not its long-term owner.
Independent of that, the broader architecture should continue moving toward an Edict-native agent loop. That means:
- persistence substrate below,
- Edict-native orchestration above,
- host/front-end thinning around both.

## Immediate Recommended Slice
1. Define the minimum bootstrap responsibilities and slab self-identification needed for one representative allocator attach path.
2. Add an allocator attach/detach experiment for a single allocator so mapped slab backing can become live allocator-owned state rather than decode-only input.
3. Introduce explicit restore/save-completed lifecycle metadata so the VM sees restore as a boot event, not a delayed `save()` return.
4. Keep the existing manifest/index path as transitional scaffolding until the new bootstrap path proves it can rehydrate allocator ownership and roots correctly.
