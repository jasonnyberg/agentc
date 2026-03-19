# Project Facts

- **SlabId**: Specialized hashing implemented for unordered maps.
- **TraversalContext**: Uses 8KB slab-allocated bitsets for O(1) visitation checks.
- **Cursor Pinning**: Cursors pin nodes; removal is blocked for pinned nodes.
- **Edict**: Stacks are now Listree-based (CLL) for arena compatibility.
- **FFI**: mapper extracts "type", "size", "offset", "return_type".
