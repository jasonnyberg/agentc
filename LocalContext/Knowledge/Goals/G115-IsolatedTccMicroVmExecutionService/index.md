# Goal: G115 — Isolated TCC Micro-VM Execution Service

**Status**: COMPLETE
**Created**: 2026-06-21
**Parent/Track**: TinyCC Native Interoperability
**Depends On**: 🔗[G114 — Edict TCC Surface and Module Wrappers](../G114-EdictTccSurfaceModuleWrappers/index.md)

## Objective

Move TinyCC compile+execute for untrusted/generated code into an isolated micro-VM worker process with structured result envelopes, crash containment, and timeout/cancellation behavior.

## Rationale

TCC-generated code is native code. A generated function pointer is valid only inside the process that compiled it, and a bad generated C program can segfault or loop forever. The production-safe shape is a worker process that compiles and executes locally, then reports results through AgentC's existing worker/Root1 infrastructure.

## Acceptance Criteria

- [x] TCC compile+execute can run in an exec-spawned worker process without crashing the coordinator on generated-code timeout/failure.
- [x] Worker returns structured envelopes for success, compile error, missing symbol, runtime failure, timeout, and cancellation.
- [x] Compile+execute happens in the same worker process; raw function pointers are never sent through Listree/mmap messages.
- [x] Coordinator can set compile and execution timeouts.
- [x] Worker result transfer uses ordinary envelopes over pipes; no raw process pointers cross the boundary.
- [x] Tests cover successful execution, compile failure, worker crash containment, and timeout/cancel behavior.
- [x] Documentation records that the isolated exec path is the primary runtime path for this host.

## Progress

### 2026-06-23
- Replaced the earlier in-process/fork-only direction with a dedicated helper executable launched by `posix_spawn`, matching the user's preference for a distinct process boundary.
- `edict/tcc_runtime.cpp` now tracks async jobs, pid/read-fd state, timeout enforcement, collection, and cancellation envelopes.
- `edict/tcc_worker_exec_main.cpp` + `edict/tcc_worker_native.cpp` now compile and run the generated C inside the helper process and report results back through a structured envelope protocol.
- `TccRuntimeTest.IsolatedRunCollectsResultThroughWorkerExec` and `IsolatedRunTimesOutAndReturnsFailureEnvelope` validate the success and timeout paths.

### 2026-06-24
- `agentc_tcc_worker_exec` now seeds TinyCC's runtime library path from `AGENTC_TCC_ROOT`, which lets the isolated helper use the local `~/tinycc/libtcc1.a` instead of depending on the system install.
- The working local helper configuration on this host requires `~/tinycc` to be configured with `--with-selinux`; without it, generated-code relocation/execution trips TinyCC's `mprotect failed` path.
- Added explicit runtime-failure, cancellation, and crash-containment coverage via `IsolatedRunNonZeroEntryReturnsRuntimeFailureEnvelope`, `IsolatedRunCancelReturnsCancellationEnvelope`, and `IsolatedRunCrashReturnsSignalEnvelope`.
- The isolated helper-exec path now has verified envelopes for success, compile error, missing entry symbol, runtime failure, timeout, cancellation, and crash/signal containment.
- Latest validation passed: `./build/edict/edict_tests --gtest_filter=TccRuntimeTest.*` (12/12), full `./build/edict/edict_tests` (210/210), and `cmake --build build-no-tcc --target edict_tests -j4`.

## Implementation Notes

This should reuse lessons from G091/G107/G110:

- bounded task envelopes
- private worker `workspace`
- process-isolated launch modes
- Root1-compatible job/status envelopes
- no provider/runtime handles in worker context/imports

See 📄[WP — TinyCC Native Interoperability Plan](../../WorkProducts/WP_G112_TinyccNativeInteropPlan.md).
