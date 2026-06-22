# Goal: G115 — Isolated TCC Micro-VM Execution Service

**Status**: PLANNED
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability
**Depends On**: 🔗[G114 — Edict TCC Surface and Module Wrappers](../G114-EdictTccSurfaceModuleWrappers/index.md)

## Objective

Move TinyCC compile+execute for untrusted/generated code into an isolated micro-VM worker process with structured result envelopes, crash containment, and timeout/cancellation behavior.

## Rationale

TCC-generated code is native code. A generated function pointer is valid only inside the process that compiled it, and a bad generated C program can segfault or loop forever. The production-safe shape is a worker process that compiles and executes locally, then reports results through AgentC's existing worker/Root1 infrastructure.

## Acceptance Criteria

- [ ] TCC compile+execute can run in a fork/fork-exec worker process without crashing the coordinator on generated-code failure.
- [ ] Worker returns structured envelopes for success, compile error, missing symbol, runtime failure, timeout, and cancellation.
- [ ] Compile+execute happens in the same worker process; raw function pointers are never sent through Listree/mmap messages.
- [ ] Coordinator can set compile and execution timeouts.
- [ ] Worker result transfer uses ordinary Listree/JSON envelopes; no raw process pointers cross the boundary.
- [ ] Tests cover successful execution, compile failure, worker crash containment, and timeout/cancel behavior.
- [ ] Documentation records that in-process `tcc.run!` is developer/unsafe-mode only, if it exists at all.

## Implementation Notes

This should reuse lessons from G091/G107/G110:

- bounded task envelopes
- private worker `workspace`
- process-isolated launch modes
- Root1-compatible job/status envelopes
- no provider/runtime handles in worker context/imports

See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
