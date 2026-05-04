# G020 — Early Type Binding (`bindTypes()`)

**Status**: COMPLETE  
**Started**: 2026-03-21  
**Completed**: 2026-03-21  
**Parent**: G018 — FFI LTV Passthrough  
**Dashboard**: 🔗[Dashboard](../../../../Dashboard.md)

---

## Objective

Eliminate the `ns` (namespace) parameter from the entire boxing API by adding a
post-parse type-binding pass (`bindTypes()`) inside `Mapper::materialize()`.
After parsing, field types of the form `"struct X"` are resolved into `type_def`
CPtr children on the field node — so callers never need to carry or pass a
namespace argument.

---

## Motivation

The `ns` param was a workaround: because struct field types were just strings
(e.g. `"struct Point"`), the boxing code had to re-query the parser namespace at
box/unbox time to find the matching type definition.  This:
- Polluted the entire public API (`box()`, `unbox()`, `packStruct()`, `unpackStruct()`)
- Required callers to supply a namespace they didn't logically own
- Made the C ABI awkward (`agentc_box(LTV source, LTV typeDef, LTV ns)`)

With `bindTypes()`, each field node already carries a pointer to its resolved
`type_def`, so `ns` is never needed at use time.

---

## Design

`bindTypes()` is called at the end of `Mapper::materialize()`, immediately after
the symbol tree is built.  It walks every field node in the type tree:

```
for each symbol (struct/union):
  for each field child:
    if field["type"] is a "struct X" string:
      look up "X" in the same Mapper namespace
      if found: add field["type_def"] = <CPtr to resolved type node>
```

This is a pure forward-only pass — no cycles possible because C does not allow
self-referential value types (only pointer-to-self which is a separate type_def).

---

## Changes Made

### `cartographer/mapper.h` / `mapper.cpp`
- Added `void bindTypes()` private method; called at end of `materialize()`
- Walk: for each named symbol child whose `"kind"` is `"Field"`, if `"type"` resolves
  to a known struct name, add `"type_def"` child pointing to that struct's symbol entry

### `cartographer/boxing.h` / `boxing.cpp`
- Removed `ns` parameter from:
  - `Boxing::box(LTV source, LTV typeDef)` (was `box(LTV source, LTV typeDef, LTV ns)`)
  - `Boxing::unbox(LTV boxed)` (unchanged signature — `ns` was never needed here)
  - `Boxing::packStruct()` / `unpackStruct()` — now use `type_def` child for nested recursion

### `cartographer/boxing_ffi.h` / `boxing_export.cpp`
- Removed `ns` parameter from `agentc_box(LTV source, LTV typeDef)`
- C ABI simplified to two arguments

### `edict/edict_vm.cpp`
- `createBootstrapCuratedCartographer()` — removed `ns` argument from the
  `cartographer.box` func-def tree (was a third parameter node)

### `cartographer/tests/mapper_tests.cpp`
- Added `BindTypesCreatesTypeDef` test — verifies that after `materialize()`,
  a nested struct field has a `"type_def"` child node pointing to the correct symbol

### `cartographer/tests/test_input.h`
- Added `Rect` struct (two `Point` fields: `topLeft`, `bottomRight`) used by
  the new `BindTypesCreatesTypeDef` test

### `demo/test_boxing_ffi.sh`
- Updated to pass two args to `cartographer.box` (removed `ns` arg); expanded
  from 11 assertions to 24 assertions covering nested struct boxing

---

## Acceptance Criteria — All Met

- `cmake --build build --parallel` — clean, zero warnings
- `ctest --test-dir build` — 7/7 suites, 90/90 tests
- `bash demo/test_boxing_ffi.sh` — 24/24 assertions
- No `ns` parameter anywhere in the boxing public API

---

## Test Coverage

| Suite / Script | Tests | Status |
|---|---|---|
| `mapper_tests` (`BindTypesCreatesTypeDef`) | 1 new | ✅ |
| All 7 ctest suites | 90 | ✅ All pass |
| `demo/test_boxing_ffi.sh` | 24 (was 11) | ✅ All pass |

---

## Progress Log

- **2026-03-21**: Goal completed. `bindTypes()` implemented in `Mapper::materialize()`.
  `ns` param removed from all boxing API surfaces (C++, C ABI, edict bootstrap,
  unit tests, demo/test scripts). `BindTypesCreatesTypeDef` test added; `Rect`
  struct added to `test_input.h`. `test_boxing_ffi.sh` expanded to 24 assertions.
  7/7 suites pass (90/90 tests).
