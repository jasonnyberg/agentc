# WP — TinyCC Native Interoperability Plan

## Purpose

Design an additive TinyCC/libtcc native interoperability track for AgentC. This does **not** replace the existing Cartographer + clang/libffi mechanism. It adds a second path optimized for runtime-generated C adapters that compile in-memory and are invoked through fixed, directly-callable native signatures.

## Source Material and Evidence

### User-provided source

`~/ffi_alternative.txt` records a conversation about runtime-generated C, TinyCC, direct function-pointer invocation, micro-VM isolation, and symbol-cache linking. The application examples in that file (game engine / visualization / shaders) are intentionally treated here as illustrative, not as scope for the first implementation.

Key takeaways from the conversation:

- TinyCC/libtcc can compile C strings to memory and expose symbols via raw function pointers (`tcc_compile_string`, `tcc_relocate`, `tcc_get_symbol`).
- Function pointers are only valid inside the compiling process; they cannot be shared across processes through mmap slabs as pointer values.
- Arbitrary native signatures still need either libffi or a fixed wrapper ABI. For AgentC, the better first path is a fixed AgentC call ABI that generated C code targets.
- TinyCC compiles C, not C++. C++ code should be exposed through `extern "C"` wrappers or host-registered symbols via `tcc_add_symbol`.
- Headers and external libraries are possible, but include paths/libraries/symbols should be explicitly whitelisted.
- TinyCC does not provide a useful reflection/AST/type-extraction API. Keep Cartographer/libclang for parsing/reflection, or use C ABI annotations for controlled APIs.

### Current implementation evidence

- `cartographer/CMakeLists.txt` links Cartographer against libclang and libffi (`pkg_check_modules(LIBFFI REQUIRED libffi)`) and builds `cartographer` from `mapper.cpp`, `ffi.cpp`, `parser.cpp`, `protocol.cpp`, `resolver.cpp`, and `service.cpp`.
- `cartographer/ffi.h` / `ffi.cpp` implement the current generic dynamic invocation mechanism: `loadLibrary`, `invoke`, `createClosure`, libffi type mapping, dlopen handles, blocklist enforcement, and Listree conversion.
- `edict/edict_vm_ffi.cpp` exposes the current Edict import path (`resolver.import!`, `resolver.import_resolved!`, deferred import/status/collect, parser/resolver JSON flow) over Cartographer. This path remains authoritative for clang-reflected external libraries.
- `/usr/include/libtcc.h` on this host exposes the needed libtcc API: `tcc_new`, `tcc_set_output_type`, `tcc_add_include_path`, `tcc_add_library_path`, `tcc_add_library`, `tcc_add_symbol`, `tcc_compile_string`, `tcc_relocate`, `tcc_get_symbol`, `tcc_output_file`, `tcc_list_symbols`.
- `/usr/lib64/libtcc.a` exists and a local smoke program linked it successfully: `add42(10,32)=84` via in-memory compile + direct C++ function-pointer call.

Confidence: High. The API and current AgentC architecture were checked directly in the repository and on the host.

## Architectural Position

AgentC should have **two native interop paths**:

| Path | Purpose | Keep / Add |
|------|---------|------------|
| Cartographer + clang/libffi | Runtime reflection/import of existing C/C++ libraries, signature extraction, dynamic arbitrary ABI calls, callbacks | Keep |
| TinyCC/libtcc | Runtime-generated C adapters, fixed AgentC ABI, direct function-pointer calls, fast in-memory compile, micro-VM-local execution | Add |

The TinyCC path should initially target **generated C adapter functions**, not arbitrary third-party C/C++ ABI calls. Cartographer already handles arbitrary native signatures. TinyCC's strength is compiling a small generated adapter that speaks a host-defined signature.

## Non-Goals for the First Track

- Do not replace `resolver.import!`, Cartographer, libclang, or libffi.
- Do not attempt to compile C++ with TinyCC.
- Do not make TCC-generated function pointers durable or cross-process-shareable.
- Do not let generated code arbitrarily `#include` system headers or link arbitrary libraries by default.
- Do not pass stateful provider/runtime handles to generated C code.
- Do not expose raw mmap slab internals as a stable C ABI before a dedicated slab-native API exists.
- Do not make shader generation or visualization canvases part of the first implementation; those remain future applications.

## Core Design

### 1. Optional build dependency

Add an optional CMake feature, e.g. `AGENTC_ENABLE_TCC`, that probes:

- Header: `/usr/include/libtcc.h` or CMake `find_path(LIBTCC_INCLUDE_DIR libtcc.h)`
- Library: `/usr/lib64/libtcc.a` or `find_library(LIBTCC_LIBRARY NAMES tcc libtcc)`

When unavailable, AgentC builds unchanged and `tcc.*` words are absent or return a structured `tcc_unavailable` envelope.

### 2. Process-local TCC service object

Add a small runtime component, likely under `tcc/` or `edict/tcc/`:

- `TccCompilerService`
- `TccModule`
- `TccSymbolRegistry`
- `TccDiagnostic`

The service owns `TCCState*` lifetimes and compiled code lifetimes. A compiled module handle is process-local and should be represented in Edict as a volatile native handle envelope, not durable Listree state.

### 3. Fixed AgentC TCC ABI

Do not call arbitrary signatures directly in the first implementation. Instead, generated C code must export a known symbol with a known signature, for example:

```c
#include "agentc_tcc_api.h"

int agentc_tcc_entry(agentc_tcc_call* call) {
    const char* a = agentc_tcc_arg_text(call, 0);
    const char* b = agentc_tcc_arg_text(call, 1);
    long out = agentc_tcc_parse_i64(a) + agentc_tcc_parse_i64(b);
    agentc_tcc_result_i64(call, out);
    return 0;
}
```

Host-side C++ casts only this fixed signature:

```cpp
using AgentcTccEntry = int (*)(agentc_tcc_call*);
```

This bypasses libffi without pretending arbitrary ABI calls are solved. The generated adapter code performs argument interpretation using helper functions that AgentC provides through `tcc_add_symbol`.

### 4. Virtual AgentC C header

Provide a stable generated C header, either as an actual file (`include/agentc_tcc_api.h`) or a virtual prepended string:

```c
typedef struct agentc_tcc_call agentc_tcc_call;
const char* agentc_tcc_arg_text(agentc_tcc_call* call, int index);
int agentc_tcc_arg_count(agentc_tcc_call* call);
void agentc_tcc_result_text(agentc_tcc_call* call, const char* text);
void agentc_tcc_result_i64(agentc_tcc_call* call, long long value);
void agentc_tcc_result_f64(agentc_tcc_call* call, double value);
void agentc_tcc_log(agentc_tcc_call* call, const char* message);
```

MVP arguments can be strings only. Later phases can add binary primitive arrays, LTV handles, and safe slab-buffer descriptors.

### 5. Whitelisted symbol injection

Use `tcc_add_symbol` as the primary linker/symbol-cache mechanism:

- AgentC helper symbols (`agentc_tcc_arg_text`, `agentc_tcc_result_*`, logging)
- Optional math symbols (`sin`, `cos`, etc.) from a preloaded whitelist
- Later: exported C ABI façades over internal libraries or DeltaGUI backend capabilities

This avoids arbitrary dynamic linking by default and lets AgentC reject code that references unavailable symbols.

### 6. Edict surface

Expose a new capsule/module separate from `resolver`:

```edict
"int agentc_tcc_entry(agentc_tcc_call* call) { ... }" tcc.compile! @mod
mod ["10" "32"] tcc.run! @result
result to_json! print
```

Likely public words:

| Word | Stack | Result |
|------|-------|--------|
| `tcc.available!` | `-- status` | TCC availability/version envelope |
| `tcc.compile!` | `source -- module` | Process-local compiled module handle or error envelope |
| `tcc.run!` | `module args -- result` | Executes `agentc_tcc_entry` through fixed ABI |
| `tcc.symbols!` | `module -- symbols` | Lists global symbols via `tcc_list_symbols` for diagnostics |
| `tcc.drop!` | `module -- status` | Deletes module / releases `TCCState` |
| `tcc.policy!` | `-- policy` | Include paths, allowed symbols, mode |

The Edict module `tcc.edict` should be a thin wrapper over C++ VM primitives or imported LTV primitives, following the G111 pattern: keep public composition in Edict where possible, but use C++ for process-local native handles.

### 7. Isolation model

TCC-generated code is unsafe native code. The safe production shape is compile+execute in the same isolated worker process:

- Direct function pointer calls are valid only in the compiling process.
- A crash should kill only that micro-VM worker, not the coordinator.
- Result transfer returns Listree/JSON envelopes to the coordinator.
- Shared data should be copied or passed through controlled mmap resources; raw process pointers do not cross process boundaries.

MVP may allow in-process test execution behind `unsafe`/developer gates, but the planned product surface should be process-isolated.

### 8. Relationship to DeltaGUI

Longer term, AgentC can leverage `~/DeltaGUI` by exposing a **C ABI façade** over selected backend capabilities, not by asking TinyCC to compile C++ directly. Candidate shape:

1. DeltaGUI backend exposes `extern "C"` functions or a separate adapter library.
2. AgentC preloads that adapter and registers whitelisted symbols into TCC via `tcc_add_symbol`.
3. Generated C adapters call those stable C functions to query/transform backend data.
4. Integration happens in an isolated tool micro-VM so backend handles and credentials remain controlled.

This is intentionally downstream of the core TCC substrate.

## Security and Safety Policy

TinyCC is a native-code execution facility, not a sandbox.

Required guardrails:

- Default deny arbitrary includes, libraries, and symbols.
- Provide an explicit allowlist for include paths and host symbols.
- Compile generated code in a worker process for untrusted inputs.
- Add timeout/cancellation for compile and execution.
- Never persist raw function pointers in Listree/mmap state.
- Treat compiled module handles like process-local volatile resources.
- Add structured diagnostics for compile errors; do not dump arbitrary code into logs unless requested.
- Consider seccomp/rlimits later for TCC worker processes.

## Suggested Goal Track

### G112 — TinyCC Build Probe and Native Compile Spike

Add optional libtcc discovery, a minimal C++ wrapper, and tests proving in-memory compile + direct pointer call.

### G113 — AgentC TCC Fixed-ABI Runtime Service

Implement `TccCompilerService` with the fixed `agentc_tcc_entry(agentc_tcc_call*)` ABI, helper symbols, diagnostics, and module lifecycle.

### G114 — Edict `tcc` Surface and Module Wrappers

Expose `tcc.available!`, `tcc.compile!`, `tcc.run!`, `tcc.symbols!`, and `tcc.drop!` from Edict without touching existing `resolver`/Cartographer words.

### G115 — Isolated TCC Micro-VM Execution

Move untrusted compile+execute into process-isolated workers with structured result envelopes, crash containment, and timeout/cancellation.

### G116 — Native Symbol Cache and C ABI Bridge

Add a controlled dlopen/dlsym symbol cache that registers whitelisted host functions into TCC via `tcc_add_symbol`; support C headers / virtual headers for exposed APIs.

### G117 — AgentC / DeltaGUI Market Data Hub Integration

Downstream application/integration track: rebuild the DeltaGUI backend direction as an AgentC-hosted market-data pub/sub hub for provider subscription configuration, normalized stock/option events, configurable history, highlight publication, and DeltaGUI/agent subscribers. The original C ABI façade remains the first transitional bridge slice: expose one selected safe/read-only `~/DeltaGUI` or backend capability through an AgentC-owned `extern "C"` adapter and make it usable from isolated AgentC/TCC worker adapters.

## Interaction with G075

G075 remains the long-arc speculative-native-architecture goal. The TinyCC track is complementary:

- G075 asks how Edict should orchestrate multi-branch reasoning with rollback.
- The TinyCC track asks how an AgentC worker can generate and execute fast native C adapters.

A future G075 demonstration could use TCC-generated evaluators or heuristics, but G075 should not block the TCC substrate and the TCC substrate should not redefine G075.

## Recommended First Implementation Slice

The smallest useful slice is G112:

1. Add optional CMake detection for libtcc.
2. Add a tiny `tcc_runtime` library or `edict_tcc` component that can compile a C string to memory.
3. Require a single symbol name: `agentc_tcc_entry`.
4. Use a fixed C++ function pointer signature.
5. Pass only string args in the first test.
6. Return only text / i64 in the first test.
7. Add one demo executable and one Edict-level availability test if the feature is enabled.

This gives a real executable foundation without disturbing Cartographer.

## Open Questions

- Should `AGENTC_ENABLE_TCC` default ON when libtcc is present, or remain explicit developer opt-in?
- Should first Edict exposure be a bootstrap VM capsule (`tcc.compile!`) or an imported primitive module (`tcc.edict`) following the G111 pattern?
- How much of the helper C API should be actual installed header vs virtual injected header?
- Should worker-process isolation be mandatory before any Edict-facing `tcc.run!` is exposed, or can in-process execution exist behind `unsafe_extensions_allow!` for tests?
- What is the smallest AgentC market-data hub slice worth building first: fixture/provider ingest and canonical event schemas, or a DeltaGUI/backend C ABI façade probe feeding the hub?
