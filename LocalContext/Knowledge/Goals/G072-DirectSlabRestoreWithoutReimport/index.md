# Goal: G072 - Direct Slab Restore Without Full Library Re-import

## Status
**INVESTIGATION**

## Goal
Determine whether a restored VM session can resume directly from slab data — bypassing the current `normalize_agent_root` + `fromJson` + full re-import cycle — by treating the persisted Cartographer/FFI binding trees as durable and only re-resolving the process-local symbol addresses that have changed since the last session.

## Motivation

The current restore path (`loadRoot` + `rehydrate_vm_runtime_state`) discards almost everything in the restored slabs:

1. `reattachFileBackedSlabs` — maps ALL slab data including the full Cartographer schema trees, ext/runtimeffi binding metadata, type schemas, function signatures, etc.
2. `normalize_agent_root` + `replace_vm_root_from_json_or_throw` — discards everything except 5 keys (`conversation`, `memory`, `policy`, `runtime`, `loop`), re-materialising them from JSON
3. `call_runtime_from_vm_or_throw` — re-imports cartographer, ext, runtimeffi in a fresh REPL env on every turn

This means the saved slab data for all the import/binding trees is loaded, then immediately freed, then rebuilt from scratch on every session. This is wasteful and also means slab slots churn heavily across sessions instead of reaching a stable layout.

**The hypothesis**: the binding trees (Cartographer schema, resolved function metadata, type definitions) are *structurally* durable — they describe facts about a library that do not change between process restarts as long as the library binary has not changed. Only the process-local symbol addresses (raw function pointer values resolved by `dlsym`) are stale after restart. Re-resolving those addresses is a much smaller operation than a full re-import.

## Key Questions to Investigate

### 1. Where are resolved symbol addresses stored in the slab?
- The JSON dump shows `"resolver_address":"0x7F959EB822B6"` etc. as string values in the Listree tree. Are these the actual pointers used for dispatch, or are they metadata-only strings?
- What is the actual callable representation that Cartographer uses to invoke a resolved function? Is it stored as an immediate pointer value in a `ListreeValue` (raw bytes in the inUse/items region of the slab), as a blob-allocated buffer, or only in the string representation?
- Are there any other process-local opaque handles stored in the slab (dlopen handles, file descriptors, thread-local state, etc.)?

### 2. Are the addresses the only non-portable state?
- Library load addresses change between process restarts due to ASLR (unless the loader places the library at the same VA by coincidence or if `ASLR` is disabled / `noaslr` is set).
- Is it possible to load a library at a fixed address to make addresses stable? (Not desirable in production, but clarifies the scope of the problem.)
- Are there any other fields in the resolved binding tree that are process-local rather than structurally durable?

### 3. What is the minimal re-resolution step?
- The cost of `dlopen` is **shared** between the warm and cold restore paths — cold restore also calls `dlopen` as part of the Cartographer import pipeline. It should not be counted as a cost of the warm path.
- The incremental cost delta between warm and cold restore is therefore: schema-parse + full binding-tree construction (cold) vs. N × `dlsym` calls (warm). Since `dlsym` is a hash-table lookup, warm restore should be strictly cheaper for any library that has not changed.
- If only function pointer values need to be updated, the re-resolution step is: for each resolved symbol record in the slab, call `dlsym(handle_from_dlopen, symbol_name)` and write the new address into the appropriate slab slot.
- Can this be done as a targeted slab walk (find all ListreeValue nodes that hold a function pointer) without re-running the full Cartographer import pipeline?
- What does Cartographer's internal representation look like for a resolved symbol? Is there a known field offset or flag that identifies "this value is a function pointer"?

### 4. Feasibility of a "warm restore" path
- A warm restore would:
  1. `reattachFileBackedSlabs` — map existing files (as now)
  2. Walk the slab tree looking for stale address fields
  3. Re-`dlopen` each library (if not already loaded) and `dlsym` each symbol name
  4. Patch the address fields in-place
  5. Resume directly from the restored root, including all import trees
- This preserves full slab stability: no slot churn, no slab growth, layout converges after session 1.

### 5. Library change detection
- If the library binary has changed (new version, recompile), the symbol addresses would change AND the schemas might change. A warm restore in this case is unsafe.
- Feasibility of detecting library changes: compare stored library path + mtime/inode/hash against current file at restore time.
- If library has changed, fall back to full re-import (current cold-restore path).

## Desired End State

A restore sequence that:
1. Attaches slab files directly
2. Detects whether each imported library is unchanged (by path + mtime or content hash)
3. If unchanged: re-resolves symbol addresses in-place (fast path)
4. If changed: falls back to full re-import (current path)
5. Resumes from the restored root cursor without discarding the binding trees

This would make session startup faster, stabilise the slab layout across sessions, and eliminate the per-session allocation/free churn for all the import metadata.

## Acceptance Criteria
- [ ] Determine exactly how Cartographer stores resolved function addresses in the Listree tree (field type, storage format, traversal method)
- [ ] Determine what other process-local state (if any) lives in the slab beyond function pointer values
- [ ] Produce a concrete design for the in-place address re-resolution walk
- [ ] Implement a prototype warm-restore path and validate it against `EmbeddedVmRootRestoreTest.FullTurnPersistenceAndResume`
- [ ] Implement library-change detection and fall-back to cold restore when needed
- [ ] Add regression tests proving warm restore produces the same observable VM behaviour as cold restore

## Related Goals
- 🔗[G068 - Client/Agent Split with Embedded Persistent Edict VM](../G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
- 🔗[G071 - Session-Scoped Allocator Image Persistence](../G071-SessionScopedAllocatorImagePersistence/index.md)

## Next Action
Audit how Cartographer stores resolved symbol addresses: read `cartographer/` source to find the field in the Listree tree that holds a callable function pointer, and determine whether it is a raw pointer stored as blob/immediate bytes or only as a human-readable string.
