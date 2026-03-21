# G018 — FFI LTV Passthrough (Hoist Boxing to Pure FFI)

**Status**: Active  
**Created**: 2026-03-20  
**Parent**: G001 (Codebase Review)  
**Dashboard**: 🔗[Dashboard](../../../../Dashboard.md)

---

## Objective

Hoist the boxing/unboxing mechanism out of custom VM opcodes (`VMOP_BOX`,
`VMOP_UNBOX`, `VMOP_BOX_FREE`) into pure FFI methods exposed as a C-ABI shared
library.  The VM becomes a pure instruction dispatcher; all struct-packing and
LTV-handle management is handled in C code callable over the FFI boundary.

---

## Rationale

### Why VM opcodes were originally necessary
- The VM execute loop only dispatches on opcodes — no other hook for C++ code.
- Boxing requires `calloc`, byte-offset struct packing, and returning a live
  navigable LTV — none expressible in pure Edict at the time.
- `convertReturn` had no "adopt this pointer as a live LTV handle" path.
- No C-ABI LTV library existed.

### Why this matters
- The VM should be a pure interpreter.  Business logic (struct-packing, type
  coercions) in VM opcodes is a layering violation.
- A C-ABI LTV library enables other tools (C agents, external libraries) to
  create and manipulate the same data structures used by the VM.
- `libboxing.so` is a template: future capabilities (LMDB adapters, codec
  libraries) can follow the same pattern — pure C, links against
  `libcartographer.so`, callable via FFI with `"ltv"` type passthrough.

---

## End-state Architecture

1. **`libcartographer.so`** exports a stable C-ABI LTV API
   (`ltv_api.h` / `ltv_api.cpp`): lifecycle, data access, named-child ops,
   scalar pack/unpack.
2. **FFI passthrough** for `LTV*` parameter and return types — `convertValue` /
   `convertReturn` in `ffi.cpp` extended with `"ltv"` type sentinel.
3. **C++ boundary utilities** (`cptr_to_raw` / `raw_to_cptr` / `raw_adopt_cptr`)
   in the C++ section of `ltv_api.h` — the only crossing point between slab-
   managed `CPtr<>` and raw C pointers.
4. **`libboxing.so`** (`libboxing/boxing_ffi.h` + `boxing_ffi.c`) implements
   `agentc_box`, `agentc_unbox`, `agentc_box_free` as plain C functions.
5. **Remove `VMOP_BOX`, `VMOP_UNBOX`, `VMOP_BOX_FREE`** — Phase D (deferred,
   blocked on `libboxing.so` deployment path — see Blockers below).
6. All 7 test suites pass; `demo_boxing.sh` works end-to-end.

---

## Memory Model Constraints

*(Critical — get this wrong and you get use-after-free or double-free.)*

- `CPtr<ListreeValue>` stores slab IDs, not raw pointers.  The slab allocator
  manages refcounting.  `CPtr` constructor = addref; `CPtr` destructor = decref.
- `CPtr<T>` and `Allocator<T>` are in the **global namespace** (NOT `agentc::`).
  This tripped up `ltv_api.cpp` initially.
- `cptr_to_raw(cptr)`: extracts a raw pointer WITHOUT changing refcount.  Safe
  only while the CPtr (or another owner) is alive.
- `raw_to_cptr(raw)`: wraps a raw pointer, INCREMENTING refcount (shared ownership).
- `raw_adopt_cptr(raw)`: wraps a raw pointer WITHOUT incrementing refcount.  Use
  when taking ownership of an already-owned reference (e.g. from `ltv_create_*()`).
- `cptr_release(p)` in `ltv_api.cpp`: pre-increments refcount by +1 before the
  CPtr destructs, so the caller holds refcount 1.  This is how all `ltv_create_*()`
  functions return owned references.
- C heap objects created by FFI code (e.g. `calloc`'d struct buffers) stay on
  the C heap — do NOT force them into the slab.
- The `CPtr` on the Edict data stack keeps argument LTVs alive for the duration
  of any FFI call — no separate pinning mechanism is needed.
- LTV args passed to `convertValue` with type `"ltv"`: extract raw pointer via
  `cptr_to_raw(val)`, store as `void*`.  The CPtr on the stack is the lifetime
  anchor.
- LTV returned from C function, handled by `convertReturn` with type `"ltv"`:
  the C function returns an owned raw pointer (refcount 1); use `raw_adopt_cptr()`
  to wrap it into a CPtr without an extra addref.

---

## Boxing Tree Shape

The parser produces (and `boxing_ffi.c` expects) this LTV tree structure:

```
typeDef
  "size"     → binary[4] = struct size in bytes (int32_t, little-endian)
  "children" → null LTV with named children:
    "<fieldName>"
      "kind"   → string "Field"
      "type"   → string (C type name, may be a platform typedef alias)
      "offset" → binary[4] = byte offset into struct (int32_t, little-endian)
```

Boxed LTV produced by `agentc_box` / `op_BOX`:
```
{ "__ptr": binary[sizeof(void*)], "__type": <typeDef> }
```
`__ptr` stores `&buf` — the address of the heap-allocated buffer.  Note: this
is a pointer to the buffer (a `void*` whose value is the heap address), NOT the
buffer contents directly.  This is the same convention used by `boxing.cpp`.

Unboxed LTV produced by `agentc_unbox` / `op_UNBOX`:
```
{ "<fieldName>": string, ..., "__type": <typeDef> }
```

---

## Type Canonicalization

Platform typedef aliases (e.g. `__time_t`, `__syscall_slong_t`) are resolved to
canonical C types by `canonicalType()` in `boxing.cpp`.  Key mappings (LP64
Linux x86-64):
- `long` group (8 bytes): `__time_t`, `__clock_t`, `__syscall_slong_t`,
  `__off_t`, `__off64_t`, `ssize_t`, `ptrdiff_t`, `intptr_t`, `int64_t`,
  `time_t`, `clock_t`, `size_t`, `__suseconds_t`, `suseconds_t`, etc.
- `unsigned long` group (8 bytes): `uint64_t`, `uintptr_t`.
- `int` group (4 bytes): `__int32_t`, `int32_t`, `__mode_t`, `__socklen_t`.
- `unsigned int` group (4 bytes): `__uint32_t`, `uint32_t`.
- 16-bit, 8-bit: standard `intN_t` / `uintN_t` families.

`canonicalType()` is called first in `scalarSize()`, `packScalar()`, and
`unpackScalar()`.  The dead-code fallback table that previously appeared in
`scalarSize()` after the canonical-type branches was removed in E1 (2026-03-20).

---

## FFI `"ltv"` Type Sentinel

In `ffi.cpp` (anonymous namespace, before `namespace agentc::cartographer`):
```cpp
static ffi_type ffi_type_ltv_handle = {
    0, 0, FFI_TYPE_POINTER, nullptr
};
```
Structurally identical to `ffi_type_pointer` (passes as a pointer-sized word)
but at a **distinct address** — this address is what `convertValue` /
`convertReturn` compare against to distinguish LTV passthrough from plain C
pointer passthrough.

`getFFIType("ltv")` returns `&ffi_type_ltv_handle`.

To write a function def tree that uses LTV passthrough, set:
- `"return_type"` child → string `"ltv"` for an LTV-returning function.
- Each parameter node: `"kind"` = `"Parameter"`, `"type"` = `"ltv"`.

---

## `CPtr<T>::adoptRaw(SlabId)` Pattern

`raw_adopt_cptr(v)` in `ltv_api.h` calls:
```cpp
auto si = alloc.getSlabId(lv);
return CPtr<ListreeValue>::adoptRaw(si);
```
`CPtr<T>::adoptRaw(SlabId)` is a special constructor that takes a SlabId
**without incrementing the refcount** (uses `AdoptRawSlabIdTag` trick in
`core/alloc.h`).  This is the correct way to transfer ownership of a freshly
created LTV from C code into C++.

---

## `ltv_set_named` Ownership Semantics

```c
void ltv_set_named(LTV parent, const char* name, LTV child);
```
- The tree takes its own +1 reference via `addNamedItem`.
- The **caller retains their reference** and must `ltv_unref(child)` if done
  with it.
- Pattern: `ltv_set_named(parent, "x", val); ltv_unref(val);`

---

## `ltv_get_named` Ownership Semantics

```c
LTV ltv_get_named(LTV parent, const char* name);  // BORROWED
```
Returns a borrowed reference — valid only while `parent` is alive (or another
owner holds a reference to the child).  Do NOT call `ltv_unref()` on it.

---

## `agentc_box` / `agentc_unbox` / `agentc_box_free` Contract

```c
LTV agentc_box(LTV source, LTV typeDef);   // returns OWNED (refcount 1)
LTV agentc_unbox(LTV boxed);               // returns OWNED (refcount 1)
void agentc_box_free(LTV boxed);           // frees C heap buf, does NOT ltv_unref boxed
```
- `source` and `typeDef` are **BORROWED** by `agentc_box`.
- `boxed` is **BORROWED** by `agentc_unbox` and `agentc_box_free`.
- `agentc_box_free` only frees the `calloc`'d buffer inside `__ptr`; the boxed
  LTV itself is still valid until `ltv_unref(boxed)`.
- Call order for full cleanup: `agentc_box_free(boxed); ltv_unref(boxed);`

---

## Phase D Deployment Path Problem (Blocker)

`demo_boxing.sh` currently uses `cartographer.box !`, `cartographer.unbox !`,
`cartographer.box_free !` — these are Edict VM opcodes (`VMOP_BOX` etc.).
Removing the opcodes (Phase D) breaks the demo unless we provide an alternative
Edict-level mechanism.

The alternative would be:
1. Load `libboxing.so` from within Edict (e.g. `"libboxing.so" cartographer.load !`).
2. Parse a header declaring `agentc_box(LTV source, LTV typeDef)` with `"ltv"` types.
3. Call `agentc_box` via the FFI system with the `"ltv"` type passthrough.

This is possible now that Phase B (FFI passthrough) is complete, but requires:
- An Edict-level mechanism for runtime library loading (partially exists via
  `cartographer.load`).
- A header or inline declaration for `agentc_box` with `"ltv"` parameter types.
  The parser does not currently produce `"ltv"` types — that is a synthetic type
  sentinel, not a real C type.  The function def tree would need to be constructed
  manually in Edict or via a special header convention.

**Transitional option (not yet implemented)**: keep `VMOP_BOX` etc. but replace
their body to call `agentc_box`/`agentc_unbox`/`agentc_box_free` from C.  This
requires `boxing_ffi.c` to also be compiled into `libcartographer.so` (currently
it's only in `libboxing.so`).

Phase D is **deferred** until the deployment path question is resolved.

---

## Work Items

### Phase A — Core C-ABI LTV Library

- [x] **A1** — Design the C header `ltv_api.h` (2026-03-20)
- [x] **A2** — Implement `ltv_api.cpp`; added to `libcartographer.so` (2026-03-20)
- [x] **A3** — C API smoke tests in `tests/ltv_api_tests.cpp`; 8 tests pass (2026-03-20)

### Phase B — FFI LTV Passthrough

- [x] **B1** — `convertValue` extended for `"ltv"` type: uses `cptr_to_raw()` (2026-03-20)
- [x] **B2** — `convertReturn` extended for `"ltv"` type: uses `raw_adopt_cptr()` (2026-03-20)
- [x] **B3** — C++ boundary utility round-trip tests; 4 tests pass (2026-03-20)

### Phase C — Pure C Boxing Library

- [x] **C1** — `libboxing/boxing_ffi.h`: C header declaring `agentc_box`, `agentc_unbox`, `agentc_box_free` (2026-03-20)
- [x] **C2** — `libboxing/boxing_ffi.c`: pure C implementation using `ltv_api.h` (2026-03-20)
- [x] **C3** — `libboxing.so` wired into CMake; builds cleanly; links against `libcartographer.so` (2026-03-20)
- [x] **C4** — Boxing FFI smoke tests; 4 tests pass (`BoxingFFITest.*`) (2026-03-20)

### Phase D — Transitional: VM Opcodes Delegate to C API  *(COMPLETE)*

The Phase D blocker (deployment path for `libboxing.so` from Edict scripts) was
resolved via the **transitional approach**: keep `VMOP_BOX` / `VMOP_UNBOX` /
`VMOP_BOX_FREE` but redirect their bodies to call `agentc_box` / `agentc_unbox` /
`agentc_box_free` from `libboxing.so`.

Changes (2026-03-20):
- `edict/CMakeLists.txt` — added `boxing` to `target_link_libraries(libedict ...)`.
- `edict/edict_vm.cpp` — removed `#include "../cartographer/boxing.h"` (no longer
  needed). Added `#include "../cartographer/libboxing/boxing_ffi.h"` and
  `#include "../cartographer/ltv_api.h"`.  `op_BOX`, `op_UNBOX`, `op_BOX_FREE`
  now call `agentc_box`, `agentc_unbox`, `agentc_box_free` using `cptr_to_raw` /
  `raw_adopt_cptr` boundary utilities.

**Rationale**: The `Boxing` C++ class still lives in `libcartographer.so` (not
removed yet — Phase D full removal is still deferred).  The transitional approach
proves the C API works end-to-end through the full Edict stack without requiring
a new Edict-level library-loading mechanism for `libboxing.so`.

### Phase D — Remove VM Boxing Opcodes  *(DEFERRED)*

- [ ] **D1** — Remove `VMOP_BOX`, `VMOP_UNBOX`, `VMOP_BOX_FREE` from `edict/edict_types.h`.
- [ ] **D2** — Remove `op_BOX`, `op_UNBOX`, `op_BOX_FREE` declarations from `edict/edict_vm.h`.
- [ ] **D3** — Remove `op_BOX` / `op_UNBOX` / `op_BOX_FREE` implementations from `edict/edict_vm.cpp`. Update dispatch table.
- [ ] **D4** — Remove `VMOP_BOOTSTRAP_CURATE_CARTOGRAPHER` bootstrap glue (or confirm no longer needed).
- [ ] **D5** — Build clean; all 7/7 test suites pass.

**Blocker**: Edict-level runtime library loading and inline function-def-tree
construction with `"ltv"` type annotations — see Phase D Deployment Path Problem
above.  The transitional approach (above) is the active path for now.

### Phase E — Cleanup & Validation

- [x] **E1** — Removed dead-code fallback table in `boxing.cpp::scalarSize()` (lines 97–123 eliminated; all handled by `canonicalType()`) (2026-03-20)
- [x] **E2** (transitional) — VM boxing opcodes now delegate to `agentc_box`/`agentc_unbox`/`agentc_box_free` in `libboxing.so` (2026-03-20)
- [x] **E5** — `demo/test_boxing_ffi.sh`: 11-assertion Edict-level boxing regression test; all assertions pass (2026-03-20)
- [ ] **E3** — Final check: 7/7 test suites pass; `demo_boxing.sh` runs end-to-end — **DONE** (7/7 pass, demo works)
- [ ] **E4** — Update Dashboard.md: mark G018 transitional complete, add Timeline entry

---

## Key Files

### Modified
- `cartographer/boxing.h` — `scalarSize`, `packScalar`, `unpackScalar` moved to `public:`
- `cartographer/boxing.cpp` — E1: dead-code fallback table removed from `scalarSize()`
- `cartographer/ffi.cpp` — B1/B2: `ffi_type_ltv_handle` sentinel; `"ltv"` in `getFFIType`, `convertValue`, `convertReturn`
- `cartographer/CMakeLists.txt` — A2/C3: `ltv_api.cpp` + `boxing_ffi.c` added; `boxing` shared lib target; `ltv_api_tests.cpp` + `boxing_ffi.c` in `cartographer_tests`

### Created
- `cartographer/ltv_api.h` — A1: C/C++ dual-mode LTV API header
- `cartographer/ltv_api.cpp` — A2: C++ implementation; all C functions in `extern "C"` block
- `cartographer/libboxing/boxing_ffi.h` — C1: pure C boxing API header
- `cartographer/libboxing/boxing_ffi.c` — C2: pure C implementation
- `cartographer/tests/ltv_api_tests.cpp` — A3/B3/C4: 16 smoke tests
- `demo/test_boxing_ffi.sh` — E5: 11-assertion Edict-level boxing regression test

### To modify (Phase D/E)
- `edict/edict_types.h` — D1, D4 (deferred)
- `edict/edict_vm.h` — D2 (deferred)
- `edict/edict_vm.cpp` — D3, D4 (deferred); **E2 transitional complete**
- `demo/demo_boxing.sh` — E2 (deferred; already validated through transitional path)

---

## Test Coverage Summary

| Suite | Tests | Status |
|-------|-------|--------|
| `LtvApiTest` | 8 | ✅ All pass |
| `LtvBoundaryTest` | 4 | ✅ All pass |
| `BoxingFFITest` | 4 | ✅ All pass |
| All 7 ctest suites | 90 | ✅ All pass |
| `test_boxing_ffi.sh` (E5) | 11 assertions | ✅ All pass |

Run new tests: `./build/cartographer/cartographer_tests --gtest_filter="LtvApi*:LtvBoundary*:BoxingFFI*"`
Run shell test: `bash demo/test_boxing_ffi.sh`

---

## Progress Log

- 2026-03-20 — Goal created. All 7/7 test suites passing. `demo_boxing.sh` validated. `canonicalType()` in `boxing.cpp` complete. No G018 implementation work started yet.
- 2026-03-20 — **Session 2**: Phases A1–A2, B1–B2, C1–C3 complete. `libcartographer.so` and `libboxing.so` built cleanly. 7/7 tests pass (74 total).
- 2026-03-20 — **Session 3**: A3, B3, C4 (smoke tests in `ltv_api_tests.cpp`, 16 new tests). E1 (dead-code removal in `boxing.cpp::scalarSize()`). 7/7 tests still pass (90 total). G018 index.md updated with full rationale for agent continuity.
- 2026-03-20 — **Session 4**: Transitional Phase D — VM boxing opcodes now delegate to `libboxing.so` C API. E5 — `test_boxing_ffi.sh` written (11 assertions, all pass). `edict_vm.cpp` no longer depends on `boxing.h`. 7/7 tests still pass.
