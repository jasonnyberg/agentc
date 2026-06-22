# Goal: G116 — TCC Native Symbol Cache and C ABI Bridge

**Status**: PLANNED
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability
**Depends On**: 🔗[G115 — Isolated TCC Micro-VM Execution Service](../G115-IsolatedTccMicroVmExecutionService/index.md)

## Objective

Add a controlled native symbol cache that can preload whitelisted C ABI functions and register them into TinyCC modules via `tcc_add_symbol`.

## Rationale

The conversation's most useful extension beyond raw compilation is using `dlopen`/`dlsym` as a symbol cache instead of asking TinyCC to link arbitrary libraries. AgentC can expose only selected host functions to generated C code, preserving capability discipline while enabling direct calls into existing native services.

## Acceptance Criteria

- [ ] A symbol-cache component can preload a configured library and resolve whitelisted symbols.
- [ ] Resolved symbols can be registered into a TCC compilation context with `tcc_add_symbol`.
- [ ] Generated C code can include a controlled header or virtual header declaring the whitelisted functions.
- [ ] Unauthorized symbol references fail compilation or relocation with a structured error.
- [ ] Tests prove a whitelisted host function can be called from generated C.
- [ ] Tests prove a non-whitelisted symbol is unavailable.
- [ ] Documentation explains the difference between TCC symbol-cache calls and Cartographer/libffi arbitrary imports.

## Implementation Notes

Preferred approach:

1. Keep `tcc_add_symbol` as the normal path.
2. Avoid hardcoding raw addresses into generated code unless a benchmark later proves it is necessary.
3. Treat C++ libraries through `extern "C"` adapter functions only.
4. Consider `@agentc_export` comment annotations for AgentC-owned C++ headers rather than attempting general C++ parsing.

See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
