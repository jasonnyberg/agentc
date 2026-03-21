# G018 — FFI LTV Passthrough (Hoist Boxing to Pure FFI)

**Status**: Active  
**Created**: 2026-03-20  
**Parent**: G001 (Codebase Review)  
**Dashboard**: 🔗[Dashboard](../../../../Dashboard.md)

---

## Objective

Hoist the boxing/unboxing mechanism out of custom VM opcodes (`VMOP_BOX`, `VMOP_UNBOX`, `VMOP_BOX_FREE`) into pure FFI methods exposed as a C-ABI shared library. The VM becomes a pure instruction dispatcher; all struct-packing and LTV-handle management is handled in C code callable over the FFI boundary.

---

## Rationale

### Why VM opcodes were originally necessary
- The VM execute loop only dispatches on opcodes — no other hook for C++ code.
- Boxing requires `calloc`, byte-offset struct packing, and returning a live navigable LTV — none expressible in pure Edict at the time.
- `convertReturn` had no "adopt this pointer as a live LTV handle" path.
- No C-ABI LTV library existed.

### End-state architecture
1. A **core C-ABI shared library** (`libagentc_core.so` or similar) exposing LTV/LTI/LTVR operations as raw-pointer C functions.
2. **FFI passthrough** for `LTV*` parameter and return types — `convertValue` / `convertReturn` extended to recognize an `LTV*` token as a live-LTV handle rather than a scalar or binary blob.
3. **`cptr_to_raw` / `raw_to_cptr`** conversion functions exposed in the core library — the only crossing point for slab-managed `CPtr<>` across the FFI boundary.
4. A **`libboxing.so`** implementing `box`, `unbox`, `box_free` as plain C functions using the core C API — no VM internals required.
5. **Removal of `VMOP_BOX`, `VMOP_UNBOX`, `VMOP_BOX_FREE`** and their op handlers from the VM.
6. All 7 test suites continue to pass; `demo_boxing.sh` continues to work.

---

## Memory Model Constraints

- `CPtr<ListreeValue>` stores slab IDs, not raw pointers; `cptr_to_raw` / `raw_to_cptr` are the boundary conversions.
- C/C++ heap objects created by FFI code (e.g. `calloc`'d struct buffers) stay on the C heap — it is **not** reasonable to make arbitrary FFI-created values conform to the CPtr/slab mechanism.
- The core library and boxing `.so` link against the **same slab allocator instance** as the VM (as `libcartographer.so` already does) — this is what makes raw-pointer passing safe across the boundary.
- The `CPtr` on the data stack keeps argument LTVs alive for the duration of any FFI call — no separate pinning mechanism is needed.

---

## Work Items

### Phase A — Core C-ABI LTV Library

- [ ] **A1** — Design the C header `ltv_api.h`: enumerate the LTV operations needed by boxing (`ltv_create`, `ltv_set_child`, `ltv_get_child`, `ltv_get_scalar`, `ltv_free`, `cptr_to_raw`, `raw_to_cptr`). No implementation yet — just the interface.
- [ ] **A2** — Implement `ltv_api.cpp` with thin wrappers around existing C++ `ListreeValue` / `Slab` operations. Build it into the shared library. Confirm link succeeds.
- [ ] **A3** — Write a minimal C test (no Edict VM) that calls `ltv_create` / `ltv_set_child` / `ltv_free` via `dlopen` to confirm the C-ABI is correct.

### Phase B — FFI LTV Passthrough

- [ ] **B1** — Extend `convertValue` in `ffi.cpp` to recognize an `LTV*` token type. When the Edict argument type annotation is `"ltv"` (or similar sentinel), pass the raw pointer directly instead of encoding as binary/string.
- [ ] **B2** — Extend `convertReturn` in `ffi.cpp` to recognize an `LTV*` return type. When the called C function returns an `LTV*`, wrap it in a `CPtr` and push it onto the data stack as a live LTV handle.
- [ ] **B3** — Add a round-trip test (C++ unit test, no demo script) calling a trivial C function `LTV* identity_ltv(LTV* v)` via the FFI layer to confirm `convertValue` → `convertReturn` passthrough.

### Phase C — Pure C Boxing Library

- [ ] **C1** — Write `libboxing/boxing_ffi.h`: C header declaring `box(const char* typedef_name, LTV* values)`, `unbox(LTV* boxed)`, `box_free(LTV* boxed)`.
- [ ] **C2** — Implement `libboxing/boxing_ffi.c` using `ltv_api.h` + `calloc`. Mirror the logic currently in `boxing.cpp` but in C, with no dependency on VM internals.
- [ ] **C3** — Wire `libboxing.so` into the CMake build. Confirm it builds cleanly and links against `libagentc_core.so`.
- [ ] **C4** — Write a C-only smoke test (no VM) that calls `box` / `unbox` / `box_free` via `dlopen` on a synthetic `timeval`-shaped type definition.

### Phase D — Remove VM Boxing Opcodes

- [ ] **D1** — Remove `VMOP_BOX`, `VMOP_UNBOX`, `VMOP_BOX_FREE` from `edict/edict_types.h`.
- [ ] **D2** — Remove `op_BOX`, `op_UNBOX`, `op_BOX_FREE` declarations from `edict/edict_vm.h`.
- [ ] **D3** — Remove `op_BOX` / `op_UNBOX` / `op_BOX_FREE` implementations from `edict/edict_vm.cpp`. Update the dispatch table.
- [ ] **D4** — Remove `VMOP_BOOTSTRAP_CURATE_CARTOGRAPHER` bootstrap glue (or confirm it is no longer needed after phase C). Remove from `edict_types.h` and `edict_vm.cpp`.
- [ ] **D5** — Build clean with no references to removed symbols. All 7/7 test suites pass.

### Phase E — Cleanup & Validation

- [ ] **E1** — Remove dead-code fallback table in `boxing.cpp::scalarSize()` (redundant after `canonicalType()` was added). Confirm tests still pass.
- [ ] **E2** — Update `demo_boxing.sh` to call `box`/`unbox`/`box_free` via the FFI path (loading `libboxing.so`) rather than via VM opcodes.
- [ ] **E3** — Final check: 7/7 test suites pass; `demo_boxing.sh` runs end-to-end successfully.
- [ ] **E4** — Update Dashboard.md: mark G018 complete, add Timeline entry.

---

## Key Files

### To modify
- `cartographer/ffi.h`, `cartographer/ffi.cpp` — B1, B2
- `cartographer/boxing.cpp` — E1 (dead-code cleanup)
- `edict/edict_types.h` — D1, D4
- `edict/edict_vm.h` — D2
- `edict/edict_vm.cpp` — D3, D4
- `demo/demo_boxing.sh` — E2
- `CMakeLists.txt` — A2, C3

### To create
- `core/ltv_api.h` — A1
- `core/ltv_api.cpp` — A2
- `cartographer/libboxing/boxing_ffi.h` — C1
- `cartographer/libboxing/boxing_ffi.c` — C2

---

## Discoveries (from prior session)

- `timedefs.timespec` dot-navigation works via `op_REF` → `Cursor::resolve` → `getValue()` = raw `CPtr` at that node.
- Edict dict literal syntax: `{ "key": "value" }` (JSON-style, colons + quoted keys).
- Parser stores platform typedef spellings, not canonical C types (`__time_t`, `__syscall_slong_t`, etc.). `canonicalType()` in `boxing.cpp` maps these to primitives.
- `__suseconds_t` is 8 bytes (`long`) on LP64 Linux.
- `struct timeval` is in `sys/time.h`, not `time.h`.

---

## Progress Log

- 2026-03-20 — Goal created. All 7/7 test suites passing. `demo_boxing.sh` validated. `canonicalType()` in `boxing.cpp` complete. No G018 implementation work started yet.
