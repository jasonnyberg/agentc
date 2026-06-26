# Goal: G112 — TinyCC Build Probe and Native Compile Spike

**Status**: COMPLETE
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability

## Objective

Add the first optional TinyCC/libtcc capability probe to AgentC and prove that the host can compile a C string in memory and call the resulting function pointer directly.

## Rationale

AgentC already has Cartographer + clang/libffi for existing native libraries. TinyCC should add a separate path for runtime-generated C adapters. The first goal is deliberately small: optional build integration plus a direct-call smoke test, without altering the existing `resolver.import!` path.

## Acceptance Criteria

- [x] CMake can detect `libtcc.h` and `libtcc` / `libtcc.a` behind an explicit optional flag such as `AGENTC_ENABLE_TCC`.
- [x] AgentC builds unchanged when TinyCC is unavailable or the helper executable is absent; `libedict` no longer needs to link libtcc directly.
- [x] A minimal native helper compiles a C source string with `tcc_compile_string`, relocates it, resolves `agentc_tcc_entry`, and invokes it through a typed function pointer.
- [x] A focused test proves in-memory compile + direct call returns the expected value.
- [x] A focused test or demo records diagnostics for compile errors without crashing the VM.
- [x] Documentation/state notes now describe the TinyCC path as additive and separate from Cartographer/libffi.

## Initial Verification Evidence

A host-level smoke outside the repo linked `/usr/lib64/libtcc.a` and called an in-memory generated function successfully: `add42(10,32)=84`.

## Progress

### 2026-06-23
- `edict/CMakeLists.txt` now detects `libtcc.h` / `libtcc.a`, builds `agentc_tcc_worker_exec` when available, and keeps `libedict` buildable without directly linking the non-PIC static archive.
- `edict/tcc_worker_native.cpp` now performs the actual libtcc compile/relocate/direct-call path inside the helper executable.
- `edict/tcc_runtime.cpp` now coordinates process-local module/job handles and talks to the helper over ordinary envelope pipes.
- Focused validation passed: `./build/edict/edict_tests --gtest_filter=TccRuntimeTest.*` (4/4).
- Full validation passed after the slice landed: `./build/edict/edict_tests` (202/202).

### 2026-06-24
- `edict/CMakeLists.txt` now exposes `AGENTC_ENABLE_TCC` plus `AGENTC_TCC_ROOT`, and a separate `build-no-tcc` configuration proved `AGENTC_ENABLE_TCC=OFF` still builds `edict_tests` unchanged.
- `~/tinycc` now supports `./configure --enable-static-pic`; the working local AgentC path additionally uses `--with-selinux`, producing a PIC `libtcc.a` plus `libtcc1.a` that the helper can execute safely.
- Added negative-path TCC validation for malformed source, missing entry symbol, and unauthorized symbol relocation failure.
- Latest validation passed: `./build/edict/edict_tests --gtest_filter=TccRuntimeTest.*` (12/12), full `./build/edict/edict_tests` (210/210), and `cmake --build build-no-tcc --target edict_tests -j4`.

## Implementation Notes

- Use `/usr/include/libtcc.h` and `/usr/lib64/libtcc.a` on this host as the initial detection target, but keep CMake generic.
- Treat compiled modules as process-local, non-durable handles.
- The host only has a static non-PIC `libtcc.a`, so the implementation intentionally compiles/runs through a dedicated helper executable instead of linking libtcc into `libedict.so`.
- See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
