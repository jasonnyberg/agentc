# Goal: G116 — TCC Native Symbol Cache and C ABI Bridge

**Status**: COMPLETE
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability
**Depends On**: 🔗[G115 — Isolated TCC Micro-VM Execution Service](../G115-IsolatedTccMicroVmExecutionService/index.md)

## Objective

Add a controlled native symbol cache that can preload whitelisted C ABI functions and register them into TinyCC modules via `tcc_add_symbol`.

## Rationale

The conversation's most useful extension beyond raw compilation is using `dlopen`/`dlsym` as a symbol cache instead of asking TinyCC to link arbitrary libraries. AgentC can expose only selected host functions to generated C code, preserving capability discipline while enabling direct calls into existing native services.

## Acceptance Criteria

- [x] A symbol-cache component can preload a configured library and resolve whitelisted symbols.
- [x] Resolved symbols can be registered into a TCC compilation context with `tcc_add_symbol`.
- [x] Generated C code can include a controlled header or virtual header declaring the whitelisted functions.
- [x] Unauthorized symbol references fail compilation or relocation with a structured error.
- [x] Tests prove a whitelisted host function can be called from generated C.
- [x] Tests prove a non-whitelisted symbol is unavailable.
- [x] Documentation explains the difference between TCC symbol-cache calls and Cartographer/libffi arbitrary imports.

## Progress

### 2026-06-23
- `edict/tcc_runtime.cpp` now implements `allowLibrarySymbol`, `allowProcessSymbol`, and `clearAllowedSymbols` as an explicit allowlist cache.
- The helper process re-resolves allowed symbols locally and registers them into each `TCCState` with `tcc_add_symbol`.
- `edict/tests/tcc_test_adapter.cpp` provides a minimal AgentC-side `extern "C"` C++ adapter façade (`agentc_tcc_test_scale`, `agentc_tcc_test_provider_name`) to validate the bridge shape without touching `~/DeltaGUI`.
- `TccRuntimeTest.CompileRunListSymbolsAndDropUsingLibraryAllowlist`, `IsolatedRunCollectsResultThroughWorkerExec`, and the Edict builtin test all validate the whitelisted library path.

### 2026-06-24
- `TccRuntimeTest.CompileWithoutAllowedSymbolReturnsRelocateError` now proves the bridge fails closed when generated C references a non-whitelisted native symbol.
- The allowlist bridge also passes against the local PIC TinyCC build selected through `AGENTC_TCC_ROOT=/home/jwnyberg/tinycc`.
- Latest validation passed: `./build/edict/edict_tests --gtest_filter=TccRuntimeTest.*` (12/12), full `./build/edict/edict_tests` (210/210), and `cmake --build build-no-tcc --target edict_tests -j4`.

## Implementation Notes

Preferred approach:

1. Keep `tcc_add_symbol` as the normal path.
2. Avoid hardcoding raw addresses into generated code unless a benchmark later proves it is necessary.
3. Treat C++ libraries through `extern "C"` adapter functions only.
4. Consider `@agentc_export` comment annotations for AgentC-owned C++ headers rather than attempting general C++ parsing.

See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
