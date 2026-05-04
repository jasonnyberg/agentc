# Fact: Listree Traversal and Copy Cycle Detection
- AgentC's `Listree` traversal (via `ListreeValue::traverse`) implements cycle detection using `TraversalContext`.
- It tracks both `absolute_visited` (global visit tracking) and `recursive_visited` (recursion path tracking) to prevent infinite loops in cyclic Listree graphs.
- `ListreeValue::copy(int maxDepth, void* ctx_ptr)` also uses `TraversalContext` for cycle detection: if a node is already on the call stack (`is_recursive`), copy returns a null `CPtr` to break the cycle rather than recursing infinitely.
- The cycle detection context is passed as `void* ctx_ptr` (a `std::shared_ptr<TraversalContext>*`) so it propagates through the recursive copy without changing the public API surface.
- VM cursor environments commonly contain cyclic graphs (closure thunks that capture a reference to their defining scope), so this cycle detection is required for any traversal or copy of a live VM root.
- Logic is already compliant with the safety standards established in J2.
