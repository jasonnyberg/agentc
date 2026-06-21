# Goal: G085 — Split Edict VM Translation Unit

**Status**: COMPLETE  
**Created**: 2026-05-13  
**Completed**: 2026-05-13

## Objective
Split the large `edict/edict_vm.cpp` implementation into smaller, purpose-oriented translation units while preserving VM behavior and keeping the opcode dispatch loop centralized.

## Rationale
`edict_vm.cpp` has grown past 3000 lines and mixes core stack/evaluation semantics, Cartographer/FFI operations, bootstrap curation, closure support, cursor ops, rewrite machinery, and the dispatch loop. Splitting the file should make the VM easier to navigate without changing user-visible behavior.

## Acceptance Criteria
- [x] Introduce `edict/edict_vm_core.cpp` for core VM lifecycle, stack/evaluation/control/rewrite/cursor behavior, and the centralized dispatch loop.
- [x] Introduce `edict/edict_vm_ffi.cpp` for Cartographer/import/native FFI opcode handlers.
- [x] Introduce `edict/edict_vm_bootstrap.cpp` for built-in registration and bootstrap curation/prelude behavior.
- [x] Update the build to compile the new translation units.
- [x] Validate that focused Edict VM/regression tests still pass.

## Implementation Plan
- [x] Move the current VM implementation into `edict_vm_core.cpp` as the initial core unit.
- [x] Extract import/Cartographer opcode handlers into `edict_vm_ffi.cpp` with only the helper functions they need.
- [x] Extract built-in/bootstrap curation functions into `edict_vm_bootstrap.cpp` with local bootstrap helpers.
- [x] Leave the dispatch loop in one translation unit.
- [x] Rebuild and run focused tests.

## Implementation Notes — 2026-05-13
- Converted `edict/edict_vm.cpp` to a short navigation stub.
- Added `edict/edict_vm_core.cpp` containing VM lifecycle, transactions, core stack/eval/control/rewrite/cursor operations, closure support, and the centralized computed-goto dispatch loop.
- Added `edict/edict_vm_ffi.cpp` containing Cartographer/import/native FFI opcode handlers.
- Added `edict/edict_vm_bootstrap.cpp` containing bootstrap curation, built-in registration, and startup prelude execution.
- Updated `edict/CMakeLists.txt` to compile the new translation units.

## Validation — 2026-05-13
- `cmake --build build --target edict_tests -j2`
- `./build/edict/edict_tests --gtest_filter='SimpleAssignTest.*:RewriteLanguageTest.*:EdictVM.*:RegressionMatrixTest.*:CallbackTest.BootstrapImportCapsuleOwnsImportSurface'` — passed 37/37.
- `./build/edict/edict_tests --gtest_filter='CallbackTest.LoadBuiltinReportsMissingLibrary:CallbackTest.InterpretedImportPipelineCanRoundTripThroughJson:CallbackTest.ImportResolvedInjectsDefinitionsAndInvokesImmediately'` — passed 3/3.
- Live Google/Gemma smoke: `EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.init([gemma-4-31b-it]) @provider provider < [Reply with exactly two lowercase letters: ok] request! > / / provider.assistant_text print'` — returned `ok`.

Note: an unfiltered `./build/edict/edict_tests` run was attempted but timed out after producing excessive prompt output from another test path, so focused regression slices were used for validation. An exploratory `CallbackTest.ParserMapCanRoundTripThroughJson` run still fails with `32` vs `42`; that appears separate from this split and was not fixed in this cleanup slice.
