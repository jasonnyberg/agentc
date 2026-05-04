# Fact: ListreeValue Serialization Payload Format

**Scope**: LOCAL  
**Status**: Active  
**Last Referenced**: 2026-05-04

## Format

`ArenaPersistenceTraits<ListreeValue>::exportSlot` writes a binary payload consumed by `restoreSlot`. The layout is strictly ordered:

1. `SlabId` — list CLL slab id (two `uint32_t` via `appendSlabId`)
2. `SlabId` — tree AATree slab id
3. `uint32_t` — export flags (SlabBlob/Immediate/Free stripped; ReadOnly stripped; Duplicate added)
4. `size_t` — data length (`dlen`)
5. `int` — pinned count
6. **size-prefixed blob** — written via `appendBytes(payload, ptr, dlen)`, which writes `size_t dlen` then `dlen` bytes

`restoreSlot` always calls `readBytes` for field 6, which reads the size prefix first. **`appendBytes` must always be called exactly once**, even when `dlen == 0` (in that case it writes `size_t(0)` with no trailing bytes, which `readBytes` consumes correctly).

## Known Bug (fixed 2026-05-04)

An earlier null-data guard wrapped the entire `appendBytes` call in `if (dlen > 0)`. When `dlen == 0`, `appendBytes` was skipped entirely, but `restoreSlot` still called `readBytes`, which then read garbage as the size field. This caused `restoreSlabImages` to return `false` for any `ListreeValue` node whose data length was zero.

**Correct guard**: only substitute zeroes for the body bytes when `getData() == nullptr && dlen > 0`. Always call `appendBytes` exactly once.

## saveRoot and Read-Only Nodes

`ListreeValue::copy()` short-circuits on read-only nodes by returning the existing `CPtr` (no new slab allocation). `SessionStateStore::saveRoot` takes a checkpoint before copy, then calls `exportSlabImagesSince(checkpoint)`. If the root contains read-only nodes, those nodes' slabs predate the checkpoint and are not exported — the image set is empty, and save fails.

**Fix**: `saveRoot` uses `fromJson(toJson(root))` to force-allocate a fresh Listree tree from the JSON representation. All nodes are new allocations after the checkpoint, so export works correctly. A side-effect benefit: non-serializable transient data (live FFI handles, etc.) is naturally filtered out.
