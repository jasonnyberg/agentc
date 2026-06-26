# Goal: G114 — Edict TCC Surface and Module Wrappers

**Status**: COMPLETE
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability
**Depends On**: 🔗[G113 — AgentC TCC Fixed-ABI Runtime Service](../G113-AgentcTccFixedAbiRuntimeService/index.md)

## Objective

Expose the TinyCC fixed-ABI runtime service to Edict as an additive `tcc` capability surface, separate from the existing `resolver`/Cartographer path.

## Rationale

The user-facing value appears when Edict can compile generated C adapters and run them with ordinary Listree/string arguments. This surface must not disturb `resolver.import!`, `resolver.import_resolved!`, or the clang/libffi mechanism.

## Acceptance Criteria

- [x] Edict has a `tcc` capsule/module with availability/status reporting.
- [x] `tcc.compile!` accepts a C source string and returns a structured process-local module handle or error envelope.
- [x] `tcc.run!` accepts a module handle plus argument list and returns a structured result envelope.
- [x] `tcc.symbols!` lists compiled global symbols for diagnostics.
- [x] `tcc.drop!` releases the module handle.
- [x] Existing `resolver.*` and Cartographer tests continue to pass unchanged.
- [x] Edict-level tests cover compile/run, error envelope, symbol listing, and lifecycle/drop.
- [x] Documentation clearly distinguishes `tcc.*` generated-C adapters from `resolver.*` native library imports.

## Progress

### 2026-06-23
- Added VM opcodes and Edict plumbing for `tcc.available!`, `tcc.compile!`, `tcc.run!`, `tcc.symbols!`, `tcc.drop!`, `tcc.start_isolated!`, `tcc.status!`, `tcc.collect!`, `tcc.cancel!`, `tcc.allow_process_symbol!`, `tcc.allow_library_symbol!`, and `tcc.clear_symbols!`.
- Added the Edict-side envelope marshalling implementation in `edict/edict_vm_tcc.cpp`.
- `TccRuntimeTest.EdictBuiltinsExposeCompileRunAndAllowlistSurface` now exercises the VM opcode path directly.
- Full `./build/edict/edict_tests` remains green at 202/202, covering the existing Cartographer/`resolver.*` surface unchanged.

### 2026-06-24
- The Edict `tcc.*` surface now runs end-to-end against the local PIC TinyCC build selected through `AGENTC_TCC_ROOT=/home/jwnyberg/tinycc`, not just the system libtcc install.
- Added explicit Edict-level regression coverage for compile-error envelopes plus symbol-list/drop lifecycle handling in `TccRuntimeTest.EdictBuiltinsCompileErrorReturnsEnvelope` and `TccRuntimeTest.EdictBuiltinsSymbolsAndDropCoverLifecycle`.
- `TccRuntimeTest.EdictBuiltinsExposeCompileRunAndAllowlistSurface` remains green alongside the broader TinyCC negative-path/runtime suite.
- Latest validation passed: `./build/edict/edict_tests --gtest_filter=TccRuntimeTest.*` (12/12) and full `./build/edict/edict_tests` (210/210).

## Suggested Edict Shape

```edict
"int agentc_tcc_entry(agentc_tcc_call* call) { ... }" tcc.compile! @module
module ["10" "32"] tcc.run! @result
result to_json! print
module tcc.drop! @drop_status
```

See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
