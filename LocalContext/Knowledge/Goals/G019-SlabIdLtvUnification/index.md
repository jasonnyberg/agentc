# G019 — SlabId LTV Type Unification

**Parent Goal**: G018 — FFI LTV Passthrough  
**Status**: COMPLETE  
**Started**: 2026-03-21  
**Completed**: 2026-03-21

---

## Summary

Eliminate `typedef void* LTV` by replacing it with `typedef SlabId LTV` (32-bit opaque slab handle). Fold `libboxing.so` into `libcartographer.so` by replacing the 225-line pure-C `boxing_ffi.c` reimplementation with a ~25-line C++ thin wrapper (`boxing_export.cpp`). Add `CPtr<T>::release()` to `core/alloc.h`.

---

## Motivation

1. **Raw pointer leaks into C code**: `typedef void* LTV` exposes a C++ heap pointer to C callers. C code cannot meaningfully use it, but the type allows accidental dereferencing.
2. **O(N) slab scan bug**: `raw_adopt_cptr(void* v)` calls `Allocator::getSlabId(const T*)` which does a full slab scan to reverse-map a raw pointer → SlabId. With `LTV = SlabId` this path is eliminated entirely; the SlabId is already in the LTV.
3. **Unnecessary second shared library**: `libboxing.so` is a 225-line pure-C reimplementation of `Boxing::box`/`unbox`/`freeBox`. The only reason it existed as C was the `void*`-based LTV API. Switching to `SlabId` allows a trivial C++ thin wrapper that calls the real implementation directly.
4. **Missing `CPtr<T>::release()`**: The workaround `cptr_release()` in `ltv_api.cpp` (calls `modrefs(+1)` before CPtr destructs to achieve net-zero) is fragile. A proper `release()` method zeroes the CPtr's SlabId so the destructor skips deallocation.

---

## Key Decisions

| Decision | Rationale |
|---|---|
| `typedef uint32_t SlabId` in C, `typedef SlabId LTV` in both C and C++ | C never decodes bits; using the same size/layout (4 bytes) gives ABI compatibility on x86-64 |
| `ffi_type_ltv_handle = FFI_TYPE_UINT32` | LTV is 32 bits, not a pointer. Fixes libffi ABI. |
| No refcount guard layer | Local CPtrs are lifetime anchors for the FFI call window; `ltv_borrow` addref/decref is inherent in CPtr construction; C code cannot stash LTV handles beyond call duration. |
| Fold boxing_export.cpp into cartographer lib | Eliminates dlopen/dlsym round-trip, second .so load, 225-line C reimplementation |

---

## Design: New Boundary Utilities (replacing raw_to_cptr / cptr_to_raw / raw_adopt_cptr)

```cpp
// CPtr<ListreeValue> → LTV: O(1), no refcount change
inline LTV cptr_to_ltv(const CPtr<ListreeValue>& p) { return p.getSlabId(); }

// LTV → CPtr (BORROW: addref on construction, decref on destruction — net zero for passthrough args)
inline CPtr<ListreeValue> ltv_borrow(LTV v) { return CPtr<ListreeValue>(v); }

// LTV → CPtr (OWN: no addref — for when C fn returns owned reference at refcount 1)
inline CPtr<ListreeValue> ltv_adopt(LTV v) { return CPtr<ListreeValue>::adoptRaw(v); }
```

---

## Work Units

| WU | Description | Status |
|---|---|---|
| WU1 | `CPtr<T>::release()` in `core/alloc.h` | ✅ done |
| WU2 | `LTV` typedef + boundary utilities in `cartographer/ltv_api.h` | ✅ done |
| WU3 | Update `cartographer/ltv_api.cpp` — remove `cptr_release`, update casts | ✅ done |
| WU4 | Update `cartographer/ffi.cpp` — `FFI_TYPE_UINT32`, `convertValue`/`convertReturn` | ✅ done |
| WU5 | Create `cartographer/boxing_ffi.h` (move from `libboxing/`) | ✅ done |
| WU6 | Create `cartographer/boxing_export.cpp` (~25-line C++ thin wrapper) | ✅ done |
| WU7 | Update `cartographer/CMakeLists.txt` — add `boxing_export.cpp`, remove `boxing` target | ✅ done |
| WU8 | Update `edict/CMakeLists.txt` + `edict/edict_vm.cpp` — remove `boxing` dep, update include | ✅ done |
| WU9 | Delete `cartographer/libboxing/` directory | ✅ done |
| WU10 | Final validation + Dashboard/Timeline update | ✅ done |

---

## Acceptance Criteria (overall)

- `cmake --build build --parallel` — clean, zero warnings
- `ctest --test-dir build` — 7/7 suites, 90/90 tests
- `bash demo/test_boxing_ffi.sh` — 11/11 assertions
- `bash demo/demo_boxing.sh` — end-to-end clean
- `libboxing.so` no longer produced by the build

---

## Affected Files

| File | Change |
|---|---|
| `core/alloc.h` | Add `CPtr<T>::release()` |
| `cartographer/ltv_api.h` | New `LTV` typedef, new boundary utilities |
| `cartographer/ltv_api.cpp` | Remove `cptr_release`, update all boundary calls |
| `cartographer/ffi.cpp` | `FFI_TYPE_UINT32`, update `convertValue`/`convertReturn` LTV branches |
| `cartographer/boxing_ffi.h` | CREATE (moved from `libboxing/boxing_ffi.h`) |
| `cartographer/boxing_export.cpp` | CREATE — C++ thin wrapper delegating to `Boxing` class |
| `cartographer/CMakeLists.txt` | Add `boxing_export.cpp`; remove `boxing` shared lib target |
| `edict/CMakeLists.txt` | Remove `boxing` from `target_link_libraries` |
| `edict/edict_vm.cpp` | Update include path; update boundary utility calls |
| `cartographer/libboxing/boxing_ffi.h` | DELETE |
| `cartographer/libboxing/boxing_ffi.c` | DELETE |

---

## Progress Log

- **2026-03-21**: Goal created. Architecture finalised in G018 session 5. No files written yet.
- **2026-03-21**: WU1–WU10 complete. All acceptance criteria met: build clean, 7/7 suites pass (90/90 tests), `test_boxing_ffi.sh` 11/11, `demo_boxing.sh` end-to-end clean. `libboxing.so` eliminated. Also updated `cartographer/tests/ltv_api_tests.cpp` to use new boundary API (`cptr_to_ltv`, `ltv_borrow`, `ltv_adopt`) and `LTV_NULL` throughout (was `nullptr`).
