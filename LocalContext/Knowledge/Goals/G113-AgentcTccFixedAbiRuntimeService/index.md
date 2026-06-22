# Goal: G113 — AgentC TCC Fixed-ABI Runtime Service

**Status**: PLANNED
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability
**Depends On**: 🔗[G112 — TinyCC Build Probe and Native Compile Spike](../G112-TinyccBuildProbeNativeCompileSpike/index.md)

## Objective

Implement a process-local `TccCompilerService` that compiles generated C adapters against a fixed AgentC call ABI and invokes them without libffi.

## Rationale

TinyCC cannot solve arbitrary runtime calling conventions by itself. The safe/direct path is to require generated C code to export a known symbol with a known signature, e.g. `int agentc_tcc_entry(agentc_tcc_call*)`. The generated C adapter can interpret AgentC arguments through helper functions, while the VM performs one typed native function-pointer call.

## Acceptance Criteria

- [ ] `TccCompilerService` owns `TCCState*` lifecycle and compiled module handles.
- [ ] Generated code is required to export a fixed entry symbol, initially `agentc_tcc_entry`.
- [ ] Host invokes the entry through one precompiled function-pointer type, not libffi.
- [ ] MVP call ABI supports string arguments and text / integer result helpers.
- [ ] AgentC helper functions are exposed to TinyCC via `tcc_add_symbol`.
- [ ] A virtual or physical `agentc_tcc_api.h` declares the helper API consumed by generated C.
- [ ] Tests cover success, missing symbol, compile error, and helper-result return.
- [ ] Module handles are documented as process-local volatile resources, never persisted in Listree/mmap state.

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
