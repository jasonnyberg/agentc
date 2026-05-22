# Work Product: Static Slab Ownership Audit

**Date**: 2026-05-19  
**Goal**: 🔗[G105 — ReadOnly Static Slab Ownership Model](../../Goals/G105-ReadOnlyStaticSlabOwnershipModel/index.md)  
**Related**: 🔗[G103 — Build-Time Static Core Declaration Image MVP](../../Goals/G103-BuildTimeStaticCoreDeclarationImageMvp/index.md), 🔗[G091 — Intern Worker Concurrency MVP](../../Goals/G091-InternWorkerConcurrencyMvp/index.md), 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md), 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md)

## Purpose

Record the current allocator/Listree ownership facts that block true OS-read-only static slab mounting, and define the safe first ownership boundary before G103 moves from metadata-only declaration images to mmap/read-only slab images.

## Summary

The current allocator can persist and reattach mmap-backed slabs, but those mappings are writable and include mutable ownership metadata. A true static/core slab cannot use the current `CPtr`/allocator path unchanged because ordinary reads, cursor navigation, retain/release, and destruction can write into slab-resident metadata.

Therefore, the preferred G105 first model remains:

> Static/core slabs are immortal for their image/lease lifetime. References to static nodes must not mutate static slab `inUse`, node-local `pinnedCount`, or other slab-resident operational metadata. Any mutable tracking must be process-local sidecar state or private dynamic slab state.

## Evidence from current code

### Allocator refcounts are slab-resident and mutated by normal `CPtr` use

Primary file: `core/alloc.h`

Current behavior:

- Each allocator slab stores an `inUse` array beside the item array.
- Heap slabs allocate `ownedInUse`; file-backed slabs map `[inUse][items]` into one writable mmap region.
- `Allocator<T>::allocRaw()` increments `inUse` through `modrefs(si)`.
- `Allocator<T>::tryRetain(...)` increments `inUse` for a copied `CPtr`.
- `CPtr` destructor calls `Allocator<T>::deallocate(...)`, which decrements `inUse`, may run destructors, and may delete the whole slab.
- `CPtr::operator=(...)` also decrements/retains by mutating `inUse`.

Implication:

- A `CPtr<ListreeValue>` that points into an OS-read-only static slab would fault or corrupt invariants on copy/destruction unless static retains/releases are made no-op or redirected to a sidecar.

### Current mmap slab support is writable persistence, not static read-only mounting

Primary file: `core/alloc.h`

Current behavior:

- `Allocator<T>::createOwnedSlab(...)` for `MmapFile` uses `PROT_READ | PROT_WRITE` and `MAP_SHARED`.
- File layout is `[SLAB_SIZE * sizeof(size_t) inUse][SLAB_SIZE * sizeof(T) items]`.
- `reattachFileBackedSlabs(...)` also remaps with `PROT_READ | PROT_WRITE`.
- `flushMappedSlabs()` msyncs the full mapped region, including both `inUse` and items.

Implication:

- Existing mmap support is a durable writable allocator image, useful for session persistence, but not a static declaration/core image model.
- G103 static images should not initially reuse this writable allocator path as if it were a read-only core slab model.

### Cursor pinning writes into `ListreeValue::pinnedCount`

Primary files: `listree/listree.h`, `core/cursor.cpp`

Current behavior:

- `ListreeValue` contains `std::atomic<int> pinnedCount`.
- `ListreeValue::pin()` and `unpin()` mutate that node-local atomic.
- `Cursor::pinPath()` / `unpinPath()` call `current->pin()` / `current->unpin()` during normal cursor lifetime and navigation.

Implication:

- Cursor traversal of a static OS-read-only node would write into the static mapped node.
- Static nodes need no-op pin/unpin, sidecar pin accounting, or a separate borrowed-static cursor mode before true read-only mapping.

### Structured persistence serializes mutable operational fields

Primary file: `listree/listree.h`

Current behavior:

- `ArenaPersistenceTraits<ListreeValue>::exportSlot(...)` serializes `list`, `tree`, flags, payload bytes, and `pinnedCount`.
- It strips `SlabBlob`, `Immediate`, `Free`, `Binary`, and `ReadOnly` for restored mutable nodes.
- `restoreSlot(...)` reconstructs `ListreeValue` with a restored `pinnedCount` argument.

Implication:

- Current persistence is designed for mutable restore, not immutable static images.
- Static declaration images should define their own manifest/image semantics rather than treating mutable session images as static core slabs.

### Logical `ReadOnly` is necessary but insufficient for OS read-only slabs

Primary files: `listree/listree.cpp`, `core/cursor.cpp`

Current behavior:

- G109 hardens public mutation surfaces: assignment/removal/list-pop/cleanup-pruning refuse logical `ReadOnly` branches.
- `ListreeValue::setReadOnly(recursive=true)` is a logical mutation guard, not a storage-class declaration.

Implication:

- Logical `ReadOnly` protects against semantic writes through Listree APIs.
- It does not prevent allocator refcount writes, cursor pin writes, VM frame writes, or restoration semantics that mutate slab metadata.

## First static ownership model

For G105/G103 first static slabs:

1. **Static slabs are immortal for image/lease lifetime**
   - Static nodes are never individually freed.
   - Static `inUse` should be treated as image metadata, not a live refcount.
   - Dropping the whole mounted image/lease releases the mapping, not individual `CPtr`s.

2. **Retain/release of static ids must not write the static slab**
   - `CPtr` copy/destruction for static ids must be no-op with respect to static `inUse`, or must update a process-local sidecar.
   - The first implementation should prefer no-op immortal semantics until evidence requires sidecar refcounts.

3. **Pin/unpin of static nodes must not write `pinnedCount` in static memory**
   - Cursor traversal over static nodes should use no-op pinning or a process-local pin sidecar.
   - Static nodes cannot rely on node-local pin counts for removal safety because they are immutable and not individually freed.

4. **Mutable runtime state remains out of static slabs**
   - VM stacks, instruction pointers, activation frames, resource stacks, provider/runtime handles, FFI sidecars, eventfds, pidfds, cancellation tokens, and job records remain process-local/private or Root1-owned.

5. **Static images contain declarations, not authority**
   - G103 declaration images may contain function names, signatures, schemas, safety metadata, and expected library identities.
   - Actual native handles are lazy process-local sidecars.

## Required implementation seams before true mmap read-only mounting

A future code slice needs at least one of these seams:

- `Allocator<T>` static-slab/range metadata and a fast `isStatic(slabId)` predicate.
- `CPtr`/allocator retain/release paths that skip `inUse` writes for static slab ids.
- `Allocator<T>::getPtr(...)` support for static read-only mappings without ownership writes.
- Cursor pin/unpin behavior that is no-op or sidecar-backed for static slab ids.
- Static image mount API that does not call constructors/destructors for individual static nodes as ordinary mutable slots.
- Tests that deliberately map a static image read-only and traverse/reference it without any metadata writes or faults.

## Non-goals for the first G103/G105 bridge

- Do not implement durable activation-frame restoration here; that belongs to G104/G096.
- Do not store process-local Cartographer, provider, eventfd, pidfd, thread, or VM handles in static slabs.
- Do not require full sidecar refcounting unless no-op immortal static ownership proves insufficient.
- Do not make `intern_start!` depend on static image mounting until the declaration-image path and static ownership model are separately validated.

## Recommended next step

Before true G103 mmap mounting, implement a tiny G105 static-ownership probe:

- define static slab/range metadata in the allocator without changing normal dynamic slabs;
- add an `isStatic`/immortal path for retain/release or a narrow static handle wrapper;
- add a read-only traversal/reference test that proves no `inUse`/`pinnedCount` writes occur for static nodes.

If that is too invasive, keep G103's next slice at the manifest/mount-inspection layer and avoid constructing live `CPtr`s directly into OS-read-only static slabs.
