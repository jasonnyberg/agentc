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

- [x] **A1** — Design the C header `ltv_api.h` (2026-03-20)
- [x] **A2** — Implement `ltv_api.cpp`; added to `libcartographer.so` (2026-03-20)
- [ ] **A3** — Write a minimal C test calling `ltv_create`/`ltv_set_named`/`ltv_unref` via `dlopen`

### Phase B — FFI LTV Passthrough

- [x] **B1** — `convertValue` extended for `"ltv"` type: uses `cptr_to_raw()` to pass raw pointer (2026-03-20)
- [x] **B2** — `convertReturn` extended for `"ltv"` type: uses `raw_adopt_cptr()` to adopt returned pointer (2026-03-20)
- [ ] **B3** — Add round-trip C++ unit test for LTV passthrough via FFI

### Phase C — Pure C Boxing Library

- [x] **C1** — `libboxing/boxing_ffi.h`: C header declaring `agentc_box`, `agentc_unbox`, `agentc_box_free` (2026-03-20)
- [x] **C2** — `libboxing/boxing_ffi.c`: pure C implementation using `ltv_api.h` (2026-03-20)
- [x] **C3** — `libboxing.so` wired into CMake; builds cleanly; links against `libcartographer.so` (2026-03-20)
- [ ] **C4** — C-only smoke test for `agentc_box`/`agentc_unbox`/`agentc_box_free` via `dlopen`

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
- 2026-03-20 — **Session 2**: Phases A1–A2, B1–B2, C1–C3 complete. Summary:
  - `cartographer/boxing.h`: `scalarSize`, `packScalar`, `unpackScalar` moved to public.
  - `cartographer/ltv_api.h`: C/C++ dual-mode header with full lifecycle, data access, named-child, and scalar pack/unpack API. C++ boundary utilities `cptr_to_raw`, `raw_to_cptr`, `raw_adopt_cptr` in `agentc::` namespace.
  - `cartographer/ltv_api.cpp`: C++ implementation. Fixed namespace issue (`CPtr`/`Allocator` are global, not `agentc::`).
  - `cartographer/ffi.cpp`: Added `ffi_type_ltv_handle` sentinel; `"ltv"` case in `getFFIType`, `convertValue` (uses `cptr_to_raw`), `convertReturn` (uses `raw_adopt_cptr`).
  - `cartographer/libboxing/boxing_ffi.h`: Pure C boxing API header.
  - `cartographer/libboxing/boxing_ffi.c`: Pure C implementation using `ltv_api.h` only; no C++ dependencies.
  - `cartographer/CMakeLists.txt`: Added `ltv_api.cpp` to cartographer and cartographer_tests; added `boxing` shared library target.
  - **7/7 test suites pass** (74/74 tests). `libboxing.so` builds cleanly.
