# Knowledge: Listree (Unified Data Structure)

**ID**: LOCAL:K003
**Related Goals**: LOCAL:G001, LOCAL:G002
**Tags**: #cpp, #container, #listree, #data-structure

## Overview
`Listree` is the fundamental data structure in AgentC. It unifies the concepts of a List (sequence) and a Tree (dictionary/map) into a single node type, `ListreeValue`. This allows for homogeneous data representation across the stack, heap, and code.

## Core Components

### `ListreeValue`
- **Dual Nature**: Can act as a List or a Tree (Dictionary).
- **Mode Selection**: Determined by `LtvFlags::List`.
  - If set: List Mode. Uses `list` (CLL).
  - If unset: Tree Mode. Uses `tree` (AATree).
- **Members**:
  - `CPtr<CLL<ListreeValueRef>> list`: The circular linked list for sequence data.
  - `CPtr<AATree<ListreeItem>> tree`: The binary search tree for named data.
  - **Note**: Both pointers exist in the class, not in a union. This allows for potential hybrid states (though current logic usually enforces one mode).
  - `void* data`: Payload pointer (string, binary).
  - `size_t dataLength`: Length of payload.
  - `LtvFlags flags`: Metadata (Mode, Null, Binary, etc.).
  - `pinnedCount`: For garbage collection/safety (cursors pin nodes).

### `ListreeItem`
- **Purpose**: A named bucket in a Dictionary (Tree).
- **Structure**:
  - `std::string name`: The key.
  - `CPtr<CLL<ListreeValueRef>> values`: A stack/list of values associated with this key. This supports scoping/shadowing (pushing a new value for 'x' shadows the old one).

### `ListreeValueRef`
- **Purpose**: A thin wrapper around `CPtr<ListreeValue>`.
- **Why?**: Allows `CLL` and `AATree` to store references to values rather than embedding them directly, facilitating sharing and indirection.

## Key Behaviors

### List Mode
- Uses `CLL` logic.
- `put`: Adds to the list.
- `get`: Retrieves/removes from the list.
- `forEachList`: Iterates sequence.

### Tree Mode
- Uses `AATree` logic.
- `find`: Locates (or creates) a `ListreeItem`.
- `addNamedItem` (Helper): Adds a value to a named item's stack.
- `remove`: Removes a named item (and its entire value stack) from the tree.

### Data Payload
- Can store Strings (`char*`) or Binary Blobs (`void*`).
- Flags `Binary`, `Duplicate`, `Own`, `Free` manage memory ownership.

## Usage in VM
- **Stack**: A `ListreeValue` in List Mode.
- **Dictionary/Scope**: A `ListreeValue` in Tree Mode.
- **Code**: A `ListreeValue` (sequence of opcodes/tokens).

## Known Issues / Quirks
- **Null Tree**: `remove` can leave `tree` as `nullptr`. Methods like `find` must handle this auto-vivification (fixed in LOCAL:G002).
- **Pinning**: Cursors "pin" nodes to prevent premature collection (GC logic not fully detailed here but hooks exist).
