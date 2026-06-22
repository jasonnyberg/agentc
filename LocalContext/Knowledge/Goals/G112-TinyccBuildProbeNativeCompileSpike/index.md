# Goal: G112 — TinyCC Build Probe and Native Compile Spike

**Status**: PLANNED
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability

## Objective

Add the first optional TinyCC/libtcc capability probe to AgentC and prove that the host can compile a C string in memory and call the resulting function pointer directly.

## Rationale

AgentC already has Cartographer + clang/libffi for existing native libraries. TinyCC should add a separate path for runtime-generated C adapters. The first goal is deliberately small: optional build integration plus a direct-call smoke test, without altering the existing `resolver.import!` path.

## Acceptance Criteria

- [ ] CMake can detect `libtcc.h` and `libtcc` / `libtcc.a` behind an explicit optional flag such as `AGENTC_ENABLE_TCC`.
- [ ] AgentC builds unchanged when TinyCC is unavailable or the flag is disabled.
- [ ] A minimal C++ wrapper compiles a C source string with `tcc_compile_string`, relocates it, resolves `agentc_tcc_entry` (or a named test symbol), and invokes it through a typed function pointer.
- [ ] A focused test proves in-memory compile + direct call returns the expected value.
- [ ] A focused test or demo records diagnostics for compile errors without crashing the VM.
- [ ] Documentation states this is additive and does not replace Cartographer/libffi.

## Initial Verification Evidence

A host-level smoke outside the repo linked `/usr/lib64/libtcc.a` and called an in-memory generated function successfully: `add42(10,32)=84`.

## Implementation Notes

- Use `/usr/include/libtcc.h` and `/usr/lib64/libtcc.a` on this host as the initial detection target, but keep CMake generic.
- Treat compiled modules as process-local, non-durable handles.
- Do not expose Edict-facing execution yet except maybe `tcc.available!`; keep this goal mostly C++ test/demo level.
- See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
