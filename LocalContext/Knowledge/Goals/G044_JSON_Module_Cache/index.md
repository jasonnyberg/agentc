# [G044] JSON-based Module Import Caching

## Goal
Implement a file-based JSON caching mechanism for imported modules to bypass slow `libclang` header parsing, instead of relying on complex LMDB memory pickling for external FFI handles.

## Status
- Completed 2026-03-22. Replaced `libclang` parsing bottleneck by caching the JSON output of the resolver pipeline directly into `~/.cache/agentc/`. Caching is integrated into the C++ `EdictVM::op_IMPORT()`.
- Created 2026-03-22. Supersedes G043.

## Why This Exists
During the investigation of `G043` (LMDB pickling), it became clear that persisting initialized `ListreeValue`s containing `dlopen` handles and `dlsym` function pointers introduced significant complexity, violating the safety boundaries established in `G041` and `G042`. 

Instead of pickling the VM's internal memory state, `cartographer` already produces an intermediate `resolver_json_v1` JSON structure from headers and libraries. This JSON is completely static for a given library (unless rebuilt) and can be cached to disk. The Edict VM already knows how to instantiate `ListreeValue`s and perform fresh `dlopen` / `dlsym` calls purely from this JSON (via `import_resolved_json`). 

Caching this JSON file completely eliminates the `libclang` parsing bottleneck on subsequent boots while seamlessly reusing the standard, safe mechanisms for resolving external FFI pointers.

## Scope
1. **Cache Generation**: When a library is successfully imported (`parse_json` -> `resolve_json`), the resulting `resolver_json_v1` string should be cached to disk (e.g., `libcore.so.json`).
2. **Cache Hit/Miss Logic**: Introduce logic to check for an existing JSON cache file for a requested library. 
3. **Cache Validation**: The cache must be invalidated if the source `.h` or `.so`/`.dylib` files change. A fast checksum or `mtime` validation should be used.
4. **Integration**: The standard Edict `import` mechanism should automatically leverage this cache. On a cache hit, it skips `parser` and `resolver` and directly calls `import_resolved_json` on the cached file.

## Expected Benefits
- **Simplicity**: No complex FFI pointer rehydration or custom LMDB snapshot logic required.
- **Safety**: Avoids resurrecting invalid memory addresses across process boots.
- **Speed**: Bypasses `libclang` parsing entirely on cache hits, which is the primary initialization bottleneck.
- **Debuggability**: The cached JSON files are human-readable text.

## Related Context
- Supersedes: 🔗[`G043-LmdbPickling`](../G043-LmdbPickling/index.md) (Cancelled)
- 🔗[`j3/LocalContext/Dashboard.md`](../../../Dashboard.md)