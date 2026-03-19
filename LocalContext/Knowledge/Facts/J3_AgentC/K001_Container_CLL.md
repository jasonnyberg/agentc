# Knowledge: Circular Linked List (CLL)

**ID**: LOCAL:K001
**Related Goals**: LOCAL:G001
**Tags**: #cpp, #container, #cll, #data-structure

## Overview
`CLL<T>` is a doubly-linked circular list implementation designed for the "Slab & Listree" architecture. It relies on `CPtr<T>` (Compact/Fancy Pointer) for memory addressing within the slab arena.

## Structure
- **Links**: `lnk[2]` array of `CPtr<CLL<T>>`.
  - `lnk[0]`: Previous node (conventionally). Wait, code says `fwd = true` -> `lnk[1]`. Let's re-verify.
  - `lnk[1]`: Next node.
- **Data**: `CPtr<T> data`. The payload pointer.
- **Invariant**: An empty/single node points to itself in both directions (`lnk[0] == this`, `lnk[1] == this`).

## Key Methods

### `store(CPtr<CLL<T>> &b, bool fwd = true)`
- **Purpose**: Insert node `b` into the list relative to `this`.
- **Logic**: Increments refcount of `b` (why? assumes `store` implies ownership transfer or shared ownership?). Then calls `splice`.
- **Direction**: `fwd=true` inserts `b` as the *next* node. `fwd=false` inserts `b` as the *previous* node.

### `splice(CPtr<CLL<T>> &b, bool fwd = true)`
- **Purpose**: Low-level insertion logic.
- **Mechanism**: Standard doubly-linked insertion.
  - If `fwd=true`: `this -> b -> this->next`.
  - Updates pointers of `this`, `b`, `b->prev`, and `this->next`.
  - **Crucial**: It assumes `b` is a single node or a list segment that is properly formed? Actually, the logic `bPrev = b->lnk[!fwd]` suggests it handles splicing entire lists?
  - Code: `CPtr<CLL<T>> bPrev = b->lnk[!fwd];` ... `bPrev->lnk[fwd] = thisNext;`
  - This looks like it splices the *entire ring* of `b` into the ring of `this`.
  - Yes, it merges two rings.

### `remove(bool fwd = true)`
- **Purpose**: Removes the *next* (or *prev*) node from the list.
- **Logic**:
  - `node = lnk[fwd]` (The victim).
  - Unlinks `node` from the ring.
  - Resets `node`'s links to point to itself (`node->lnk[0]=node`, `node->lnk[1]=node`).
  - **Refcounting**: `node.modrefs(-1)`. Decrements refcount of the removed node. This balances the `modrefs(1)` in `store`.
  - **Returns**: `*this`.

### `pop(bool fwd = true)`
- **Purpose**: Removes and returns the neighbor node.
- **Returns**: Reference to the removed node.

### `forEach`
- **Purpose**: Iterates through the ring.
- **Termination**: Stops when it wraps around to `start` or encounters `nullptr` (safety).

## Usage Notes
- **Memory**: Allocated in the Slab Arena.
- **Refcounting**: `store` and `remove` manage refcounts manually. This implies `CLL` nodes are refcounted objects managed by the allocator/pointer system.
- **Thread Safety**: Not thread-safe.

## Known Issues / Quirks
- `fwd` parameter determines direction: `1` is forward (next), `0` is backward (prev).
- `lnk[1]` is typically "next".
