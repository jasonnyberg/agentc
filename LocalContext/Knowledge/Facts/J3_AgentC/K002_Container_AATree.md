# Knowledge: AATree (Dictionary)

**ID**: LOCAL:K002
**Related Goals**: LOCAL:G001, LOCAL:G002
**Tags**: #cpp, #container, #aatree, #bst, #dictionary

## Overview
`AATree<T>` is a dictionary structure used to store named items (`std::string name` -> `CPtr<T> data`). 

## Implementation Details
- **Misnomer**: Despite the class name `AATree`, the current implementation in `container.h` **lacks the Skew and Split operations** characteristic of AA Trees. It behaves as a simple, **unbalanced Binary Search Tree (BST)**.
- **Node Structure**:
  - `lnk[2]`: Children (`lnk[0]` = Left/Smaller, `lnk[1]` = Right/Larger).
  - `name`: Key (`std::string`).
  - `data`: Value (`CPtr<T>`).
  - `level`: Present member (initialized to 1), but **unused** in the `add` logic. This confirms the missing balancing logic.

## Key Methods

### `add(const std::string &nodeName, const CPtr<T> &value)`
- **Purpose**: Insert or update a key-value pair.
- **Logic**:
  - Standard BST insertion.
  - **Special Case**: If the node is "empty" (empty name and null data), it populates the current node instead of creating a child. This is used for "lazy initialization" or "auto-vivification" of the root.
  - Does **not** perform rebalancing.

### `find(const std::string &nodeName)`
- **Purpose**: Retrieve a node by name.
- **Returns**: `CPtr<AATree<T>>` to the node, or `nullptr` if not found.

### `remove(const std::string &nodeName)`
- **Purpose**: Remove a node by name.
- **Logic**:
  - Uses `removeRecursive`.
  - **Returns**: The new root of the subtree. This is critical.
  - **Leaf Removal**: Returns `nullptr`.
  - **Single Child**: Returns the child (bypassing the removed node).
  - **Two Children**: Finds in-order successor (min of right subtree), copies successor's data/name to current node, and recursively removes the successor.
- **Bug/Feature**: If the root node is removed and it was the last node, it returns `nullptr`. The caller **must** handle this by updating their reference to the tree root.

## Usage in Listree
- `ListreeValue` wraps `AATree` for its "Tree Mode".
- **Critical Invariant**: `ListreeValue::tree` must be updated with the result of `tree->remove(...)`.
- **Empty State**: An empty `AATree` root pointer (`nullptr`) is valid for an empty dictionary, but methods like `find` (for insertion) must handle auto-creation.

## Future Improvements
- **Balancing**: Implement actual AA-Tree or Red-Black Tree balancing logic to ensure O(log N) performance.
- **Iterator**: `forEach` performs a simple in-order traversal.
