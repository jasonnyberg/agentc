# Goal G043 — LMDB Pickling Bootstrapping (CANCELLED)

## Status
- Cancelled. Superseded by 🔗[`G044_JSON_Module_Cache`](../G044_JSON_Module_Cache/index.md) on 2026-03-22.

## Description
Integrate LMDB-enabled slab persistence into `EdictVM` to cache and restore the entire process-local heap after parsing and resolving cartographer libraries. This bypasses the parsing step on subsequent boots, drastically improving initialization time.

## Requirements
1. **Cache Generation**: Implement a mechanism in the VM to take an LMDB snapshot of the `ListreeValue` arena (and its supporting allocators) right after bootstrapping the environment and loading initial libraries.
2. **Dynamic Library Path Serialization**: `cartographer::FFI` relies on `dlopen` handles (`ffi->loadLibrary()`) which cannot be pickled. We must serialize the loaded `.so` / `.dylib` library paths into the LMDB `ArenaRootState` metadata, and explicitly reload them during an LMDB restore operation to make sure `dlsym` works correctly.
3. **Cache Validation**: Add a mechanism to detect stale caches. The snapshot metadata must contain hashes or modification timestamps of the `.h`/`.json`/`.so` library files used to generate it. If the files change, the cache must be invalidated and regenerated.
4. **Bootstrapping Entry Point**: Modify the CLI entrypoint (e.g., `main.cpp` and `EdictVM::initResources()`) to look for the LMDB cache *before* any user script execution or heap allocation occurs. The restore operation will overwrite the current slab memory, so it must happen early.

## Success Criteria
- The LMDB cache is correctly generated and restored without leaking memory or corrupting `ListreeValue` structures.
- Restoring from the LMDB cache skips the `cartographer.import` parsing loop and successfully `dlsym` resolves functions using the natively reloaded library handles.
- Stale caches are successfully detected and regenerated.
- Unit tests (`make test`) continue to pass and `demo/test_boxing_ffi.sh` runs cleanly via the restored cache (if integrated into the tests).

## Navigation Guide for Agents
- **Bigger Picture**: Part of Phase 3, building upon the LMDB persistent arena integration completed in G016.
- **Detailed View**: See `core/alloc.h` and `core/alloc.cpp` for the `LmdbArenaStore` persistence layer that uses structured serialization. See `edict/edict_vm.cpp` for the interpreter loop. See `cartographer/ffi.cpp` for `dlopen` functionality.