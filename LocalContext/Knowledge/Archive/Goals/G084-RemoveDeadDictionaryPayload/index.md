# Goal: G084 — Remove Dead Dictionary Payload Path

**Status**: COMPLETE  
**Created**: 2026-05-13  
**Completed**: 2026-05-13

## Objective
Remove the obsolete compiler-side `Dictionary`/`VALUE_DICTIONARY`/`VMEXT_DICT` payload path while preserving JSON object literal behavior through the existing Listree runtime dictionary representation.

## Rationale
Edict JSON object literals currently emit a serialized `Dictionary` payload that the VM immediately discards when decoding `VMEXT_DICT`, replacing it with an empty Listree/null object for `VMOP_CTX_PUSH`. This creates a misleading parallel dictionary abstraction that is not the actual runtime representation.

## Acceptance Criteria
- [x] Remove the dead compiler-side `Dictionary` class and dictionary-specific `Value` API.
- [x] Remove `VALUE_DICTIONARY` and `VMEXT_DICT` serialization/decoding paths.
- [x] Preserve JSON object literal semantics by pushing a fresh null/Listree object before `VMOP_CTX_PUSH`.
- [x] Build and run focused regression coverage for JSON object literals and VM execution.

## Implementation Plan
- [x] Simplify `edict/edict_types.h` and `edict/edict_types.cpp` to keep only live `Value` payload kinds.
- [x] Update `EdictCompiler::compileJSONObject()` to avoid the dummy `Dictionary` value.
- [x] Update `EdictVM::readValue()` to remove the dictionary extension case.
- [x] Validate with focused Edict tests.

## Implementation Notes — 2026-05-13
- Removed the compiler-side `Dictionary` class and all `Value` dictionary API/storage.
- Removed `VALUE_DICTIONARY` and the `VMEXT_DICT` bytecode decode/serialize path.
- Updated JSON object literal compilation to push `Value(nullptr)` before `VMOP_CTX_PUSH`, preserving the fresh Listree object behavior without serialized dictionary payload bytes.
- Removed stale `edict/test_output.txt`, which referenced the deleted `isDictionary()` API.

## Validation — 2026-05-13
- `cmake --build build --target edict_tests -j2`
- `./build/edict/edict_tests --gtest_filter='SimpleAssignTest.*:RewriteLanguageTest.*:EdictVM.*:RegressionMatrixTest.*:CallbackTest.BootstrapImportCapsuleOwnsImportSurface'` — passed 37/37.
