# Work Product: Static Declaration Image Mount Plan

**Date**: 2026-05-19  
**Goal**: 🔗[G103 — Build-Time Static Core Declaration Image MVP](../../Goals/G103-BuildTimeStaticCoreDeclarationImageMvp/index.md)  
**Related**: 🔗[G105 — ReadOnly Static Slab Ownership Model](../../Goals/G105-ReadOnlyStaticSlabOwnershipModel/index.md), 🔗[G096 — Authoritative mmap Session Resume](../../Goals/G096-AuthoritativeMmapSessionResume/index.md), 🔗[G106 — Root1 Slab Advertisement Registry](../../Goals/G106-Root1SlabAdvertisementRegistry/index.md), 🔗[G107 — Process-Isolated Micro-VM Interns](../../Goals/G107-ProcessIsolatedMicroVmInterns/index.md), 🔗[WP — Static Slab Ownership Audit](../WP-StaticSlabOwnershipAudit-2026-05-19/index.md)

## Purpose

Define the next mount architecture after the G103 JSON-byte mmap/import slice and the G105 static-immortal ownership probe. This plan explains how to move from "read-only bytes parsed into heap Listree slots" to true static declaration images whose Listree/value slots can be mmap-mounted read-only without process-local authority handles.

## Current state

Implemented first slices:

1. `buildWorkerPrimitiveDeclarationImage()` creates a deterministic metadata-only `worker.edict` declaration image.
2. `writeDeclarationImage(...)` / `readDeclarationImage(...)` round-trip the image through JSON/Listree.
3. `readDeclarationImageMmapReadOnly(...)` maps the JSON artifact bytes with `PROT_READ`, then parses them into live Listree slots.
4. `mountDeclarationImageReadOnly(...)` validates the parsed image, recursively applies logical `ReadOnly`, and marks exact `ListreeValue` slots static-immortal through the G105 seam.
5. G105 static-immortal slots avoid `inUse` retain/release/destruction writes and avoid `pinnedCount` pin/unpin writes.

What remains missing:

- Listree/value slots themselves are not yet in an OS `PROT_READ` mapping.
- The image format is JSON text, not a fixed binary/static slot layout.
- Only `ListreeValue` slots are marked static-immortal; associated `ListreeValueRef`, `ListreeItem`, `CLL`, and `AATree` slots still live in normal dynamic allocator slabs.
- Static slot identity is process-local and not yet backed by manifest-declared slab ids/ranges.

## Target invariant

A mounted static declaration image must satisfy:

```text
mmap file bytes:       PROT_READ after construction/finalization
Listree/value slots:   readable through normal traversal APIs
ownership metadata:    no writes to static image pages during retain/release/pin/traversal
native authority:      no dlopen/dlsym/function/provider/eventfd/pidfd handles in image
mutable runtime state: private dynamic slabs or Root1 coordination sidecars only
```

## Recommended staged mount architecture

### Stage 0 — current complete bridge

- Metadata-only declaration image as JSON/Listree.
- Read-only mmap of JSON bytes.
- Parse into dynamic slots.
- Logical `ReadOnly` + static-immortal slot marking.

This proves schema, validation, handle exclusion, and no-mutate reference behavior for mounted declaration roots.

### Stage 1 — static binary declaration image container

Introduce a binary container that still deserializes into dynamic slots, but has static-image-grade manifest discipline:

```text
[StaticImageHeader]
[manifest-json-length][manifest-json]
[payload-json-length][payload-json]
[payload-hash]
```

Purpose:

- lock down byte-level file format/version/hash validation;
- keep payload metadata-only;
- avoid committing to raw allocator slot layout before G105 ownership details are stable.

This is a safe next code slice if true allocator-backed static slabs are too invasive.

### Stage 2 — static slot table image

Create a declaration-specific static slot table, separate from mutable allocator slab files:

```text
[StaticImageHeader]
[Manifest]
[ListreeValue slots]
[ListreeValueRef slots]
[ListreeItem slots]
[List/Tree node slots]
[String/blob table]
```

Rules:

- Slot tables are built/finalized in writable memory or a build artifact.
- Runtime maps them `PROT_READ`.
- Static slot ids are image-local first, then translated through a mount table.
- `CPtr` must not directly assume static image-local ids are ordinary allocator slab ids unless the allocator owns a read-only static slab layer.

Two implementation choices:

1. **Allocator-mounted static slabs**
   - Extend `Allocator<T>` with static mapped slabs distinct from dynamic slabs.
   - `getPtr(...)` resolves both dynamic and static slab ranges.
   - `tryRetain`, `modrefs`, `deallocate`, and cursor pinning no-op for static ids.
   - Requires careful slab-id range/layer partitioning.

2. **Static handle / borrowed view API**
   - Do not expose static image-local nodes as ordinary `CPtr` initially.
   - Provide read-only traversal/inspection wrappers over image-local ids.
   - Promote/copy into dynamic Listree only when a public mutable API needs ordinary `CPtr`.
   - Lower risk, but less integrated with existing Edict lookup.

Recommended first true-static direction: **allocator-mounted static slabs**, because Edict/module dictionaries eventually need normal lookup/traversal semantics over static core namespaces. Use a deliberately tiny slab-id range and test-only image first.

### Stage 3 — Root1 mount registry

Once a static slot image can be mounted locally, Root1 should own image mount identity:

- image id / hash;
- manifest path;
- mapped file descriptors or mmap regions;
- slab id/layer range;
- epoch/lease lifetime;
- process-local native binding sidecar identity.

This belongs to G106/G096, not the first G103 local mount proof.

## Static slab id/layer boundary

A true mount needs a way to distinguish static ids from dynamic ids. Candidate approaches:

1. Reserve high slab-index ranges for static/core layers.
2. Add an explicit layer tag next to `SlabId` in future handle types.
3. Keep `SlabId` for dynamic allocator slots only and use a separate `StaticSlabId`/mount table for static images.

Recommended near-term choice:

- Do not widen `SlabId` yet.
- Add allocator-local static mapped slab metadata keyed by normal slab index for the first test-only mount.
- Reserve a small high-index range by convention in the test.
- Revisit explicit layered ids under G096/G104 once activation frames and durable code-object identity are designed.

## Mount order

A safe mount flow should be:

1. Open image file read-only.
2. Map bytes read-only, or map writable-private only during builder/finalization tests.
3. Validate header/version/hash/manifest before exposing root handles.
4. Register static slab/range metadata with allocator or mount table.
5. Mark static slots/slabs immortal before any public `CPtr`/cursor access.
6. Expose only read-only root handles/views.
7. Resolve native capability calls lazily through process-local sidecars.

## Mutation/write hazards to test

A true static mount test should verify no writes for:

- copying/dropping `CPtr` to a static value;
- direct `modrefs(...)` attempts on static ids;
- direct `deallocate(...)` attempts on static ids;
- `Cursor` construction/navigation/destruction over static values;
- `ListreeValue::pin()` / `unpin()` over static nodes;
- validation/traversal/toJson over static roots;
- rejected mutation attempts through logical `ReadOnly` APIs.

For an OS-level test, map the image read-only and assert these operations do not fault and do not dirty image bytes.

## What not to store

Static declaration images must continue to exclude:

- `dlopen` handles;
- `dlsym` or function pointers;
- FFI closure/runtime handles;
- provider/session/credential handles;
- eventfd/epoll/pidfd/raw fd values;
- live VM pointers;
- mutable activation frames or instruction pointers;
- cancellation tokens/job records/resource locks.

## Recommended next code slice

Implement a **test-only static binary container** before allocator-backed raw slot mounting:

1. Add `StaticDeclarationImageHeader` with magic/version/manifest length/payload length/hash.
2. Write/read the current declaration image into that container.
3. Map the container read-only and validate header + payload hash before parsing.
4. Keep `mountDeclarationImageReadOnly(...)` as the post-parse mount seam.
5. Add a test for stale/invalid header/hash.

This creates a durable byte-format boundary without prematurely exposing raw allocator slot tables as static slabs. After that, implement allocator-mounted static slot tables with a tiny test image.
