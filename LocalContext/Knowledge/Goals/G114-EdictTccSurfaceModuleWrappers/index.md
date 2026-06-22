# Goal: G114 — Edict TCC Surface and Module Wrappers

**Status**: PLANNED
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability
**Depends On**: 🔗[G113 — AgentC TCC Fixed-ABI Runtime Service](../G113-AgentcTccFixedAbiRuntimeService/index.md)

## Objective

Expose the TinyCC fixed-ABI runtime service to Edict as an additive `tcc` capability surface, separate from the existing `resolver`/Cartographer path.

## Rationale

The user-facing value appears when Edict can compile generated C adapters and run them with ordinary Listree/string arguments. This surface must not disturb `resolver.import!`, `resolver.import_resolved!`, or the clang/libffi mechanism.

## Acceptance Criteria

- [ ] Edict has a `tcc` capsule/module with availability/status reporting.
- [ ] `tcc.compile!` accepts a C source string and returns a structured process-local module handle or error envelope.
- [ ] `tcc.run!` accepts a module handle plus argument list and returns a structured result envelope.
- [ ] `tcc.symbols!` lists compiled global symbols for diagnostics via `tcc_list_symbols` when available.
- [ ] `tcc.drop!` releases the module/TCC state.
- [ ] Existing `resolver.*` and Cartographer tests continue to pass unchanged.
- [ ] Edict-level tests cover compile/run, error envelope, symbol listing, and lifecycle/drop.
- [ ] Documentation clearly distinguishes `tcc.*` generated-C adapters from `resolver.*` native library imports.

## Suggested Edict Shape

```edict
"int agentc_tcc_entry(agentc_tcc_call* call) { ... }" tcc.compile! @module
module ["10" "32"] tcc.run! @result
result to_json! print
module tcc.drop! @drop_status
```

See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
