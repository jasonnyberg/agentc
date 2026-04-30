# Knowledge: J3 Slab Allocator

**ID**: LOCAL:K024
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #cpp, #memory, #allocator, #slab

## Overview
The J3 Slab Allocator provides a relocatable, reference-counted memory model for AgentC's data structures. It replaces standard heap allocation with a deterministic, arena-like system managed via "Fancy Pointers" (`CPtr<T>`).

## Core Components

### `Allocator<T>`
- **Type-Specific**: A static singleton allocator exists for each type `T`.
- **Structure**: An array of `Slab` pointers (`slabs[NUM_SLABS]`).
- **Slab**: A fixed-size block holding `SLAB_SIZE` items (`1<<10` in the current code).
- **Capacity**: Bounded by `NUM_SLABS * SLAB_SIZE` slots per type.
- **Reference Counting**: Maintains an `inUse` count for each slot.
- **Checkpointing**: Supports nested checkpoints with allocation logging, `checkpoint()`, `rollback(...)`, and `commit(...)`, enforcing top-most ordering for commit/rollback.

### `SlabId`
- **Handle**: A pair of `uint16_t` (Slab Index, Item Offset).
- **Size**: 32-bit lightweight handle.
- **Null**: `{0, 0}` acts as the null sentinel.

### `CPtr<T>` (Compact/Fancy Pointer)
- **Purpose**: Smart pointer wrapper around `SlabId`.
- **Behavior**:
  - **Refcounting**: Automatically increments refs on copy, decrements on destruction/overwrite.
  - **Deallocation**: Frees the slot in the Slab when refs reach 0.
  - **Dereference**: Resolves `SlabId` to `T*` via the `Allocator`.
- **Restriction**: `CPtr` itself cannot be heap-allocated (operator new deleted), forcing it to live on the stack or embedded in other Slab objects.

## Advantages
- **Relocatable**: Data is addressed by logical ID, not memory address. Slabs can be moved or serialized (mmap) without pointer swizzling (mostly).
- **Deterministic**: Allocation is sequential and tightly packed.
- **Safety**: Automatic reference counting prevents leaks (mostly, barring cycles).

## Limitations
- **Capacity**: Hard limit of ~65k objects per type.
- **Cycles**: Simple refcounting does not handle circular references (requires cycle breaker or GC sweep).
- **Fragmentation**: Free slots are reused, but simple sequential search is used for allocation.
- **Transactions**: The current checkpoint model is allocation-oriented and now supports nested checkpoints, but it still reuses free slots and is not yet a persistence-backed or shared-arena transaction system.

## 2026-03-07 Update
- `alloc.h` now provides allocator checkpoints used by the cognitive core runtime.
- `edict/edict_vm.cpp` layers VM transaction snapshots on top of allocator checkpoints so `STACK` and `DICT` frame depths can be restored together with transient allocations.
- Verification exists in `tst/alloc_tests.cpp` and `edict/tests/transaction_test.cpp`, with a runnable demonstration in `tst/demo_speculation.cpp`.
- Nested allocator checkpoints now work with top-most ordering rules, and `tst/alloc_tests.cpp` covers both nested rollback preservation and out-of-order commit rejection.
- The allocator now exposes `ArenaCheckpointMetadata` plus store-backed metadata export/restore hooks used by the active `MemoryArenaStore` and `FileArenaStore` paths; older LMDB adapter references are legacy only.
- Restart-style metadata persistence is demonstrated in `tst/demo_arena_metadata_persistence.cpp`.
