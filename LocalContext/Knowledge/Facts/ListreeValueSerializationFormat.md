# Fact: ListreeValue Serialization Format and Slab File Layout

**Scope**: LOCAL  
**Status**: Active  
**Last Referenced**: 2026-05-04

## Slab File Layout (file-backed allocators)

Native file-backed slabs created by `createOwnedSlab` use a single fixed-size file per slab index:

**`Allocator<T>` slab file** (`SLAB_SIZE * (sizeof(size_t) + sizeof(T))` bytes, fixed):
```
[SLAB_SIZE * sizeof(size_t)]  — inUse ref-count array (one size_t per slot)
[SLAB_SIZE * sizeof(T)]       — item objects
```
Both regions are mmap'd as one contiguous mapping. A single `msync` covers all tracking and object data.

**`BlobAllocator` slab file** (`sizeof(size_t) + 65536` bytes, fixed):
```
[sizeof(size_t)]  — count (byte fill level, kept in sync via BlobSlab::setCount on every write)
[65536 bytes]     — blob data
```

`inUse[offset]` is written directly into the mmap'd file region on every allocation and deallocation — no sidecar file needed. `saveRoot` (file-backed path) is therefore: `flushMappedSlabs()` (msync) + write root anchor + write bootstrap.

`loadRoot` calls `reattachFileBackedSlabs()`, which scans the backing directory, mmaps each slab file, and reads `inUse` from the start of the file. No external metadata needed.

File sizes are fixed at creation by `ftruncate` and never change. `createOwnedSlab` uses `O_CREAT|O_EXCL` — if a file already exists at that slab index, the call fails loudly rather than silently truncating existing data (which `O_TRUNC` would have done).

## ArenaPersistenceTraits<ListreeValue> Payload Format (heap-backed path)

Used by the heap-backed `saveRoot` / `loadRoot` path (`exportSlabImages` / `restoreSlabImages`). Layout per slot:

1. `SlabId` — list CLL slab id
2. `SlabId` — tree AATree slab id
3. `uint32_t` — export flags (SlabBlob/Immediate/Free/ReadOnly stripped; Duplicate added)
4. `size_t` — data length (`dlen`)
5. `int` — pinned count
6. **size-prefixed blob** — via `appendBytes(payload, ptr, dlen)`, which writes `size_t dlen` then `dlen` bytes

`restoreSlot` always calls `readBytes` for field 6, which reads the size prefix unconditionally. **`appendBytes` must always be called exactly once** even when `dlen == 0`.

## saveRoot and Read-Only Nodes (heap-backed path)

`copy()` short-circuits on read-only nodes (returns existing `CPtr`, no new allocation). The heap-backed `saveRoot` uses `fromJson(toJson(root))` rather than `root->copy()` to force-allocate all nodes after the checkpoint. Side benefit: filters out non-serializable transient data. The file-backed path does not have this problem — it does not copy or checkpoint at all.
