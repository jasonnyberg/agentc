# Goal: G113 — AgentC TCC Fixed-ABI Runtime Service

**Status**: COMPLETE
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability
**Depends On**: 🔗[G112 — TinyCC Build Probe and Native Compile Spike](../G112-TinyccBuildProbeNativeCompileSpike/index.md)

## Objective

Implement a process-local `TccCompilerService` that compiles generated C adapters against a fixed AgentC call ABI and invokes them without libffi.

## Rationale

TinyCC cannot solve arbitrary runtime calling conventions by itself. The safe/direct path is to require generated C code to export a known symbol with a known signature, e.g. `int agentc_tcc_entry(agentc_tcc_call*)`. The generated C adapter can interpret AgentC arguments through helper functions, while the VM performs one typed native function-pointer call.

## Acceptance Criteria

- [x] `TccCompilerService` owns process-local module handles, while the libtcc-owning helper process owns each request's `TCCState*` lifecycle.
- [x] Generated code is required to export a fixed entry symbol, initially `agentc_tcc_entry`.
- [x] Host invokes the entry through one precompiled function-pointer type, not libffi.
- [x] MVP call ABI supports string arguments and text / integer result helpers.
- [x] AgentC helper functions are exposed to TinyCC via `tcc_add_symbol`.
- [x] A virtual or physical `agentc_tcc_api.h` declares the helper API consumed by generated C.
- [x] Tests cover success, missing symbol, compile error, and helper-result return.
- [x] Module handles are documented as process-local volatile resources, never persisted in Listree/mmap state.

## Progress

### 2026-06-23
- Added the fixed helper ABI header at `edict/agentc_tcc_api.h`.
- Added coordinator/runtime surfaces in `edict/tcc_runtime.h` and `edict/tcc_runtime.cpp`.
- Added native helper implementation in `edict/tcc_worker_native.cpp`, including `agentc_tcc_arg_*`, parse helpers, result helpers, and typed `agentc_tcc_entry` invocation.
- `TccRuntimeTest.CompileRunListSymbolsAndDropUsingLibraryAllowlist` now proves helper-result return and fixed-entry execution against the new ABI.
- Current architecture keeps volatile module handles in the coordinator and creates per-request `TCCState*` instances inside the helper process because the host only has a static non-PIC libtcc archive.

### 2026-06-24
- Added focused negative-path coverage for malformed source (`compile_error`) and missing `agentc_tcc_entry` (`missing_entry_symbol`) without changing the fixed-entry ABI contract.
- The local-root TCC path now runs through `AGENTC_TCC_ROOT=/home/jwnyberg/tinycc`, keeping the helper-side `TCCState*` lifecycle separate from the coordinator while preserving fixed typed entry invocation.
- The goal is complete in the helper-exec architecture: `edict/tcc_runtime.cpp` owns process-local module/job handles, while `edict/tcc_worker_native.cpp` owns `TCCState*` through the per-request `TccModule` RAII wrapper.
- Latest validation passed: `./build/edict/edict_tests --gtest_filter=TccRuntimeTest.*` (12/12), full `./build/edict/edict_tests` (210/210), and `cmake --build build-no-tcc --target edict_tests -j4`.

## Implementation Notes

Candidate C ABI:

```c
typedef struct agentc_tcc_call agentc_tcc_call;
int agentc_tcc_entry(agentc_tcc_call* call);
```

Initial helpers:

```c
int agentc_tcc_arg_count(agentc_tcc_call* call);
const char* agentc_tcc_arg_text(agentc_tcc_call* call, int index);
void agentc_tcc_result_text(agentc_tcc_call* call, const char* text);
void agentc_tcc_result_i64(agentc_tcc_call* call, long long value);
void agentc_tcc_log(agentc_tcc_call* call, const char* message);
```

See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
