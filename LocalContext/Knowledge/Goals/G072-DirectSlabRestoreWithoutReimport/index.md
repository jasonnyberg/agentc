# Goal: G072 - Direct Slab Restore Without Full Library Re-import

## Status
**IN PROGRESS**

## Goal
Determine whether a restored VM session can resume directly from slab data — bypassing the current `normalize_agent_root` + `fromJson` + full re-import cycle — by treating the persisted Cartographer/FFI binding trees as durable and only re-resolving the process-local symbol addresses that have changed since the last session.

## Motivation

The current restore path (`loadRoot` + `rehydrate_vm_runtime_state`) discards almost everything in the restored slabs:

1. `reattachFileBackedSlabs` — maps ALL slab data including the full Cartographer schema trees, ext/runtimeffi binding metadata, type schemas, function signatures, etc.
2. `normalize_agent_root` + `replace_vm_root_from_json_or_throw` — discards everything except 5 keys (`conversation`, `memory`, `policy`, `runtime`, `loop`), re-materialising them from JSON
3. `call_runtime_from_vm_or_throw` — re-imports cartographer, ext, runtimeffi in a fresh REPL env on every turn

This means the saved slab data for all the import/binding trees is loaded, then immediately freed, then rebuilt from scratch on every session. This is wasteful and also means slab slots churn heavily across sessions instead of reaching a stable layout.

## Investigation Findings

1. **Cartographer Pointers**: Cartographer dynamically dispatches FFI calls. `ffi->invoke` uses `dlsym(h, funcName.c_str())` from a transient `handles_` map. The `"resolver_address"` string in the Listree is purely metadata. No raw pointers are stored for standard FFI calls!
2. **Process-Local State**: The only process-local states are `closureValue` nodes (created by `op_CLOSURE`) which hold raw `void*` pointers for ephemeral callback thunks, and the transient C++ `FFI::handles_` map.
3. **Warm Restore Mechanism**: A "warm restore" does not need to walk the tree to patch addresses because there are no addresses to patch! We only need to ensure libraries are re-`dlopen`ed. An existing helper `preload_imported_libraries` reads `__cartographer.library` tags to restore the `dlopen` handles.
4. **Current Architectural Blockers**: 
   - `rehydrate_vm_runtime_state` destructively dumps the VM root to JSON via `normalize_agent_root`, destroying non-standard keys.
   - `call_runtime_from_vm_or_throw` spins up a side-channel REPL for the `agentc_call !` on every turn, so the `@ext` and `@runtimeffi` bindings never actually make it into the main VM's persistent slab.

## Implementation Plan

1. **Non-destructive Rehydration**: Modify `normalize_agent_root` or `rehydrate_vm_runtime_state` to avoid a destructive JSON roundtrip. The configuration should be merged into the existing `ListreeValue` root so that existing imported bindings (e.g., `@ext`, `@runtimeffi`) survive.
2. **Durable Binding Import**: Move the Cartographer `resolver.import !` bootstrapping step into the MAIN VM's root during session setup (e.g., in `install_vm_agent_root` or `rehydrate_vm_runtime_state`). This makes the bindings part of the durable slab state.
3. **Direct Execution**: Replace the side-channel REPL in `call_runtime_from_vm_or_throw` with direct execution inside the main `EdictVM`.
4. **Library Handle Preloading**: Hook `preload_imported_libraries(vm, root)` into the `EdictVM` restore path / constructor to automatically reload `dlopen` handles from restored slabs.
5. **Change Detection (Future/Optional)**: Optionally verify `resolved_content_hash` or `resolved_modified_time_ns` on restore to detect if the library binary changed and force a cold restore if so.

## Acceptance Criteria
- [x] Determine exactly how Cartographer stores resolved function addresses in the Listree tree
- [x] Determine what other process-local state (if any) lives in the slab beyond function pointer values
- [x] Produce a concrete design for the in-place address re-resolution walk (Turns out to be simple preloading)
- [x] Implement a prototype warm-restore path and validate it against `EmbeddedVmRootRestoreTest.FullTurnPersistenceAndResume` (Implemented `preload_imported_libraries` and in-place Listree mutation for `rehydrate_vm_runtime_state`)
- [x] Implement Phase 2/3: Durable Binding Import and Direct Execution (removing the side-channel REPL for `call_runtime_from_vm_or_throw`)
- [x] Implement library-change detection and fall-back to cold restore when needed
- [ ] Add regression tests proving warm restore produces the same observable VM behaviour as cold restore

## Related Goals
- 🔗[G068 - Client/Agent Split with Embedded Persistent Edict VM](../G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
- 🔗[G071 - Session-Scoped Allocator Image Persistence](../G071-SessionScopedAllocatorImagePersistence/index.md)