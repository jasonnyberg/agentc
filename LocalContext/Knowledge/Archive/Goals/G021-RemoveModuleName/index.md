# G021 — Remove Module Name Parameter from Resolver Import API

## Status: COMPLETE (2026-03-22)

## Problem Statement

All four `resolver.import*` opcodes and the two high-level edict thunks accept a "module name" / "scope name" as a required argument. This name is stored as metadata (`scope` and `requested_name` fields in `__cartographer`) but is **never used for symbol resolution, type binding, namespace prefixing, or FFI dispatch**. It was always vestigial — the actual import is assigned to a variable by the caller via `@defs`, which is independent of the third argument.

With `bindTypes()` (G020) now resolving struct field types via direct LTV pointers rather than name-based lookups, there is no remaining function for the parameter.

## Goal

Remove the `scopeName` / module-name parameter from the entire resolver import call path, simplifying the API at every layer (edict source, VM opcodes, C++ service, wire protocol).

## Affected Files Summary

| Layer | File | Change |
|-------|------|--------|
| Struct fields | `cartographer/service.h:34,47` | Delete `scopeName` from `ImportRequest` and `ImportResult` |
| Method signatures | `cartographer/service.h:58,60` | Remove `scopeName` param from `importResolvedFile` and `importResolverJson` |
| Service logic | `cartographer/service.cpp` (21 lines) | Remove all reads, writes, guards, and both `__cartographer` metadata emissions |
| Wire protocol | `cartographer/protocol.cpp:405–406,455,484,550` | Remove `scope` token from encode/decode; bump protocol version |
| VM opcodes (4) | `edict/edict_vm.cpp` (12 lines) | Remove scope `popData()`, local var, and struct assignment from `op_IMPORT`, `op_IMPORT_RESOLVED`, `op_IMPORT_DEFERRED`, `op_IMPORT_RESOLVED_JSON` |
| Edict thunks (2) | `edict/edict_vm.cpp:1200,1203,1205,1207` | Remove `@scope` binding and `scope` push from `resolver.import` and `resolver.import_resolved` thunks |
| Service tests | `cartographer/tests/service_tests.cpp` (14 lines) | Remove `scopeName` assignments; remove `scope`/`requested_name` assertions; remove scope arg from direct `importResolvedFile` calls |
| Callback tests | `edict/tests/callback_test.cpp` (7 lines) | Remove `'defs` argument from all `resolver.import*` call strings |
| Language reference | `LocalContext/Knowledge/WorkProducts/edict_language_reference.md:928,942,958,974` | Remove `'mylib` from all 4 `resolver.import*` examples |
| **No change needed** | `ltv_api.h`, `boxing_ffi.h` | Public C ABI headers — neither exposes scope |
| **No change needed** | Demo shell scripts | No demo uses `resolver.import` |

## Tasks

### T1 — `cartographer/service.h`: Remove `scopeName` fields and method parameters
- Remove `scopeName` field from `ImportRequest` struct (line 34)
- Remove `scopeName` field from `ImportResult` struct (line 47)
- Remove `scopeName` parameter from `importResolvedFile` signature (line 58)
- Remove `scopeName` parameter from `importResolverJson` signature (line 60)

### T2 — `cartographer/service.cpp`: Remove all `scopeName` reads, writes, guards, and metadata emissions
Lines to update: 393, 403, 411–413 (guard), 492, 545, 552, 575, 606, 613, 631, 643–644 (guard), 751 (method def), 754, 761–763 (guard), 774, 778 (method def), 782, 789–791 (guard), 828, 867 (guard), 958

Key changes:
- Remove the `if (request.scopeName.empty())` early-return guards entirely (no replacement)
- Remove both `addNamedItem` calls for `scope` and `requested_name` in `createImportHandleValue` and `materializeImportedDefinitions`
- Remove all `result.scopeName = ...` propagation assignments

### T3 — `cartographer/protocol.cpp`: Remove scope from wire format; bump version
- Remove `scope` token from `encodeImportRequest` (line 455)
- Remove `scope` token from `encodeImportStatus` (line 484)
- Remove `scope` decode from `decodeImportRequestFields` (lines 405–406)
- Remove `resultOut.scopeName = requestOut.scopeName` from `decodeImportStatus` (line 550)
- Bump protocol version identifier (e.g. `protocol_v1` → `protocol_v2` or version field increment)

### T4 — `edict/edict_vm.cpp`: Remove scope pop from 4 opcodes and 2 thunks

**Opcodes** (remove from each):
- `op_IMPORT` (~line 925): remove `auto scopeValue = popData()`, `std::string scopeName`, validation check, `request.scopeName = scopeName`
- `op_IMPORT_RESOLVED` (~line 951): same pattern
- `op_IMPORT_DEFERRED` (~line 972): same pattern
- `op_IMPORT_RESOLVED_JSON` (~line 1097): same pattern, update direct `importResolverJson(...)` call

**Thunks** (remove `@scope` binding and `scope` push):
- `resolver.import` thunk (~line 1200): `"@scope @header @library "` → `"@header @library "`; remove `scope` push before `resolver.import_resolved_json !`
- `resolver.import_resolved` thunk (~line 1205): `"@scope @resolved_path "` → `"@resolved_path "`; remove `scope` push

### T5 — `cartographer/tests/service_tests.cpp`: Remove scope from 14 locations
- Remove `request.scopeName = "defs"` from 6 test setup blocks
- Remove `EXPECT_EQ(..., "scope", "defs")` and `EXPECT_EQ(..., "requested_name", ...)` assertions from 4 tests
- Update `importResolvedFile(path, "defs")` → `importResolvedFile(path)` in 2 test calls

### T6 — `edict/tests/callback_test.cpp`: Remove `'defs` from 7 edict source strings
- Line 449: `'defs resolver.import !` → `resolver.import !`
- Line 475: `resolved 'defs resolver.import_resolved_json !` → `resolved resolver.import_resolved_json !`
- Line 547: `'defs resolver.import !` → `resolver.import !`
- Line 597: `'defs resolver.import_deferred !` → `resolver.import_deferred !`
- Line 654: `'defs resolver.import_deferred !` → `resolver.import_deferred !`
- Line 737: `'defs resolver.import_resolved !` → `resolver.import_resolved !`
- Line 786: `'defs resolver.import_resolved !` → `resolver.import_resolved !`

### T7 — `LocalContext/Knowledge/WorkProducts/edict_language_reference.md`: Remove `'mylib` from 4 examples
- Line 928: `'./libmylib.so './mylib.h 'mylib resolver.import !` → `'./libmylib.so './mylib.h resolver.import !`
- Line 942: `resolved_json 'mylib resolver.import_resolved_json !` → `resolved_json resolver.import_resolved_json !`
- Line 958: `'./mylib.h './libmylib.so 'mylib resolver.import_deferred !` → `'./mylib.h './libmylib.so resolver.import_deferred !`
- Line 974: `resolved_json './libmylib.so 'mylib resolver.import_resolved !` → `resolved_json './libmylib.so resolver.import_resolved !`

### T8 — Verify
- `cmake --build build` — clean build
- `ctest --test-dir build` — all 7 suites pass
- `demo/test_boxing_ffi.sh` — 24/24
- `demo/test_cartographer_dump.sh` — 24/24
- `demo/test_cartographer_schema_inspect.sh` — 24/24
- `demo/test_cartographer_resolve_inspect.sh` — 22/22

## Suggested Task Order

T1 → T2 → T3 (in parallel: T3 can be done alongside T2) → T4 → T5 → T6 → T7 → T8

T1 is a prerequisite for all C++ changes (T2–T6). T4 can proceed in parallel with T2/T3 since the opcode changes are in a different file. T7 is documentation and can be done any time.

## Notes

- **No public C ABI change**: `ltv_api.h` and `boxing_ffi.h` are unaffected. This is an internal C++ API cleanup.
- **No demo script changes**: No shell demo currently exercises `resolver.import` with a scope argument.
- **Protocol version**: bumping the version string in `protocol.cpp` is important to prevent old subprocess binaries from silently misparking the message fields if a mixed deployment ever occurs.
- **Stack order preserved**: After removal, `op_IMPORT` stack order becomes `library header` (header on top) and `op_IMPORT_DEFERRED` becomes `header library` (library on top) — verify the existing order is consistent and document it in error messages.
