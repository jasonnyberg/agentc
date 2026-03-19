# Knowledge: Cursor (Navigation)

**ID**: LOCAL:K004
**Related Goals**: LOCAL:G001
**Tags**: #cpp, #container, #cursor, #navigation

## Overview
`Cursor` is a stateful navigator for traversing the `Listree` structure. It maintains a position (`current` value, `currentItem` context) and a path history, allowing for relative navigation (`up`, `down`, `next`, `prev`) and absolute path resolution.

## State
- `root`: The `ListreeValue` where the cursor started (or is anchored).
- `current`: The `ListreeValue` currently pointed to.
- `currentItem`: The `ListreeItem` (key/bucket) containing `current`, if applicable. Null if at root or in a list.
- `pathComponents`: A `ListreeValue` (List) tracking the path segments (breadcrumbs) to the current location.

## Navigation Logic

### `resolve(const std::string& path, bool insert)`
- **Purpose**: Navigate to a location specified by a dot-separated string (e.g., `a.b.c` or `.global.x`).
- **Logic**:
  - Parses path string.
  - Iterates components.
  - Handles `..` by calling `up()`.
  - Handles names by looking up in `current` (Tree Mode).
  - **Auto-Creation**: If `insert` is true, intermediate nodes are created (though verify specific behavior for *intermediate* vs *target*).
  - **Item Stack Semantics**:
    - `name` resolves the head/newest value on the named item's value stack.
    - `-name` resolves the tail/oldest value on that same item-value stack.
    - `*name` produces a history iterator over the item's values.
  - **Nested List Tail Semantics**:
    - `name-` resolves `name` first, then if that resolved value is a list, returns the tail element of that list.
    - `-name-` is the composed form: resolve the tail/oldest item value first, then if that value is a list, return the tail element of that list.

## Named Item Model
- Named items behave like LIFO stacks of values.
- Helper creation paths were consolidated so repeated writes to the same named item push at the head by default.
- `ListreeItem::addValue(...)` now defaults to head insertion, so direct item mutation matches `Cursor::assign(...)`, `Cursor::create(...)`, and `addNamedItem(...)`.
- This item-level value stack is distinct from the contents of a list value stored in an item.

## Syntax Summary
- `name`: newest/head item value.
- `-name`: oldest/tail item value.
- `name-`: tail of the resolved list value.
- `-name-`: tail of the resolved list value taken from the oldest/tail item value.
- Invalid `name-` / `-name-` style list-tail dereference against a non-list is treated as a semantic error; `Cursor::resolve(...)` records the error and Edict lookup paths surface it through AgentC's native throw/catch mechanism rather than silently treating it as a normal miss.

### Relative Moves
- `up()`: Pops the last path component, moves to parent.
- `down()`: Enters the first child (or specific logic for default entry).
- `next()` / `prev()`: Moves to siblings. relies on path analysis (e.g., parsing the last component and incrementing/decrementing if numeric, or iterating the parent's container).

### Pinning
- **Mechanism**: Calls `pin()` on the `current` `ListreeValue`.
- **Purpose**: Prevents the underlying data from being modified/freed while the cursor is focused on it (logic depends on GC implementation).

## Usage
- **VM Execution**: The VM uses cursors to track the Instruction Pointer (IP) and Data Pointer.
- **Path Resolution**: Used for variable lookup (`resolve`).

## Known Issues / Quirks
- **Reference Counting**: Cursor holds `CPtr`s, so it participates in the refcounting graph.
- **Path String**: `pathComponents` stores the path as a list of strings, which is expensive to reconstruct repeatedly. `getPath()` serializes it.
