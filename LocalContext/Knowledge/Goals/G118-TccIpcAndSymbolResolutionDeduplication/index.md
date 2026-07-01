# Goal: G118 — TCC IPC and Symbol Resolution Deduplication

**Status**: COMPLETE
**Created**: 2026-07-01
**Parent/Track**: TinyCC Native Interoperability / Code Quality
**Depends On**: 🔗[G116 — TCC Native Symbol Cache and C ABI Bridge](../G116-TccNativeSymbolCacheCAbiBridge/index.md)

## Objective

Consolidate duplicated IPC primitives, `BoundSymbol` struct, symbol-resolution logic, and envelope-construction boilerplate that were copy-pasted across the TCC integration files during the G112–G116 implementation sprint.

## Rationale

The G112–G116 sprint delivered the TCC runtime substrate quickly by copying proven IPC patterns from `edict_worker_runtime.cpp` and `cartographer/service.cpp` into the new TCC files. This was acceptable for a first implementation, but the duplication now creates a maintenance hazard: a bug fix in one `writeAll` or symbol-resolution path must be manually replicated across 2–4 files. The duplication spans:

1. **IPC primitives** (`writeAll`, `readAll`, `writeString`, `readString`, `writeStringVector`, `readStringVector`) — exact or near-exact duplicates across 4 files.
2. **`BoundSymbol` struct** — defined twice with slightly different fields.
3. **Symbol resolution** (`validateSymbolOrigin` vs `resolveSymbolSpecInCurrentProcess`) — same dlopen/dlsym logic, different return shapes.
4. **Handle cleanup** (`closeValidationHandles` vs `closeDynamicLibraryHandles`) — identical, different names.
5. **Constants** (`kProcessOrigin`, `kModeCompile`, `kModeRun`) — duplicated across anonymous namespaces.
6. **`allowProcessSymbol` / `allowLibrarySymbol`** — near-duplicate methods differing only in the origin string.
7. **VM opcode dispatch boilerplate** — 5 opcodes with identical pop→extract→call→push shape.
8. **Job error envelope construction** — 3 occurrences of manual field-copying in `status()`/`cancel()`.

## Scope

### In scope

- Extract shared IPC primitives into a reusable inline header.
- Unify `BoundSymbol` and shared TCC constants into a common header.
- Consolidate symbol-resolution logic into a single function usable by both coordinator and worker.
- Unify `allowProcessSymbol` / `allowLibrarySymbol` into a shared private helper.
- Reduce VM opcode dispatch and envelope-construction boilerplate.
- All changes must preserve the existing `TccRuntimeTest.*` 12/12 and full `edict_tests` 210/210 baseline.
- The `AGENTC_ENABLE_TCC=OFF` no-TCC build must still compile.

### Out of scope

- Refactoring `edict_worker_runtime.cpp` or `cartographer/service.cpp` to use the shared IPC header (separate cleanup, lower priority — those files predate TCC and have their own consumers).
- Changing the TCC wire protocol or envelope schema.
- Modifying the `agentc_tcc_worker_exec` binary boundary or process isolation model.
- Touching `~/DeltaGUI`.

## Acceptance Criteria

### AC1 — Shared IPC header

- [ ] Create `edict/pipe_io.h` (or similar) containing inline `writeAll`, `readAll`, `writeString`, `readString`, `writeStringVector`, `readStringVector` templates.
- [ ] `tcc_runtime.cpp` and `tcc_worker_native.cpp` include this header and no longer define their own copies.
- [ ] The header is self-contained (no dependencies beyond `<string>`, `<vector>`, `<unistd.h>`, `<cstdint>`).
- [ ] No behavior change: `TccRuntimeTest.*` passes 12/12 and `edict_tests` passes 210/210.

### AC2 — Shared `BoundSymbol` and constants

- [ ] Create `edict/tcc_shared.h` (or extend `tcc_runtime.h`) containing the canonical `BoundSymbol` struct (with the `address` field) and the `kProcessOrigin`, `kModeCompile`, `kModeRun` constants.
- [ ] Both `tcc_runtime.cpp` and `tcc_worker_native.cpp` use the shared definition.
- [ ] The coordinator side (`tcc_runtime.cpp`) tolerates the unused `address` field without warnings.

### AC3 — Unified symbol resolution

- [ ] Extract a single `resolveSymbolOrigin` function that handles both validation-only (coordinator) and resolution-with-address (worker) modes, controlled by a parameter or a thin wrapper.
- [ ] `validateSymbolOrigin` and `resolveSymbolSpecInCurrentProcess` are replaced by the unified function or thin wrappers around it.
- [ ] `closeValidationHandles` and `closeDynamicLibraryHandles` are replaced by a single shared `closeLibraryHandles` function.

### AC4 — Unified `allowProcessSymbol` / `allowLibrarySymbol`

- [ ] A single private helper (e.g. `allowSymbol(name, declaration, origin)`) implements the shared logic.
- [ ] `allowProcessSymbol` and `allowLibrarySymbol` delegate to it with `kProcessOrigin` or `libraryPath` respectively.
- [ ] No behavior change in the allowlist surface or its envelope output.

### AC5 — VM opcode dispatch consolidation

- [ ] A helper template or function reduces the pop→extract-handle→call-service→push-envelope pattern used by `op_TCC_STATUS`, `op_TCC_COLLECT`, `op_TCC_CANCEL`, `op_TCC_DROP`, `op_TCC_SYMBOLS`.
- [ ] Error messages remain specific to each opcode.

### AC6 — Job error envelope helper

- [ ] A `jobErrorEnvelope(const TccJob&, status, error)` helper replaces the 3 manual field-copying sites in `status()` and `cancel()`.
- [ ] Timeout, cancellation, and worker-exit-without-result paths all use the helper.

### AC7 — Build and test validation

- [ ] `./build/edict/edict_tests --gtest_filter=TccRuntimeTest.*` passes 12/12.
- [ ] Full `./build/edict/edict_tests` passes 210/210.
- [ ] `cmake --build build-no-tcc --target edict_tests` succeeds with `AGENTC_ENABLE_TCC=OFF`.
- [ ] No new compiler warnings under `-Wall -Wextra -Werror`.

## Implementation Steps (Dependency-Ordered)

### Step 1: Create `edict/pipe_io.h`

Create a new inline header `edict/pipe_io.h` with the IPC primitives:

```cpp
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

namespace agentc::pipe_io {

bool writeAll(int fd, const void* data, std::size_t size);
bool readAll(int fd, void* data, std::size_t size);
bool writeString(int fd, const std::string& value);
bool readString(int fd, std::string& value);
bool writeStringVector(int fd, const std::vector<std::string>& values);
bool readStringVector(int fd, std::vector<std::string>& values);

} // namespace agentc::pipe_io
```

Implementation can be inline or in a `pipe_io.cpp` compiled into `libedict.so`. The TCC worker exec (`agentc_tcc_worker_exec`) links `libedict.so` already, so both targets get the same code.

**Pitfall**: `cartographer/service.cpp` and `edict/edict_worker_runtime.cpp` also have copies. Do NOT refactor them in this goal — they have different consumers and EINTR-handling variations. Leave them as-is and note them as future cleanup candidates.

### Step 2: Create shared TCC types header

Extend `edict/tcc_runtime.h` or create `edict/tcc_shared.h` with:

```cpp
struct BoundSymbol {
    std::string name;
    std::string declaration;
    std::string origin;
    const void* address = nullptr;  // used by worker; unused by coordinator
};

constexpr const char* kProcessOrigin = "<process>";
constexpr const char* kModeCompile = "compile";
constexpr const char* kModeRun = "run";
```

Remove the duplicate definitions from both `.cpp` files' anonymous namespaces.

### Step 3: Unify symbol resolution

Create a single function in the shared header (or in `tcc_runtime.cpp` exposed via the header):

```cpp
// Resolves a BoundSymbol's origin/name to a dlopen handle + dlsym address.
// If `outAddress` is nullptr, validates only (coordinator mode).
// If `outAddress` is non-null, stores the resolved pointer (worker mode).
bool resolveSymbolOrigin(const BoundSymbol& symbol,
                        std::unordered_map<std::string, void*>& handles,
                        void*& processHandle,
                        const void** outAddress,
                        std::string& error);

void closeLibraryHandles(const std::unordered_map<std::string, void*>& handles);
```

Replace `validateSymbolOrigin` (coordinator) and `resolveSymbolSpecInCurrentProcess` (worker) with calls to this function.

### Step 4: Unify `allowProcessSymbol` / `allowLibrarySymbol`

Extract a private helper `TccCompilerService::Impl::allowSymbol(name, declaration, origin)` that both public methods delegate to.

### Step 5: Consolidate job error envelope construction

Add a free function or `Impl` helper:

```cpp
TccEnvelope jobErrorEnvelope(const TccJob& job,
                              const std::string& status,
                              const std::string& error);
```

Use it in `status()` (timeout path), `cancel()`, and `finalizeJob()` (worker-exit-without-result path).

### Step 6: Reduce VM opcode boilerplate

In `edict_vm_tcc.cpp`, extract a helper that pops a handle envelope, extracts a field (`module_id` or `job_id`), calls a service method, and pushes the result. The helper takes the field name, error message, and a lambda/fn-pointer to the service method.

### Step 7: Full build and test validation

```bash
# TCC-enabled build
cd /home/jwnyberg/agentlang/agentc
cmake --build build/edict --target edict_tests -j$(nproc)
./build/edict/edict_tests --gtest_filter=TccRuntimeTest.*
./build/edict/edict_tests

# No-TCC build
cmake --build build-no-tcc --target edict_tests -j$(nproc)
```

## Files Affected

| File | Change |
|------|-------|
| `edict/pipe_io.h` | **New** — shared IPC primitives |
| `edict/tcc_runtime.h` | **Modified** — add `BoundSymbol`, constants, shared resolution function declarations |
| `edict/tcc_runtime.cpp` | **Modified** — remove duplicated IPC code, `BoundSymbol`, constants, `validateSymbolOrigin`, `closeValidationHandles`; use shared helpers; unify `allow*Symbol`; add `jobErrorEnvelope` |
| `edict/tcc_worker_native.cpp` | **Modified** — remove duplicated IPC code, `BoundSymbol`, constants, `resolveSymbolSpecInCurrentProcess`, `closeDynamicLibraryHandles`; use shared helpers |
| `edict/edict_vm_tcc.cpp` | **Modified** — extract opcode dispatch helper, reduce boilerplate |
| `edict/CMakeLists.txt` | **Modified** — add `pipe_io.cpp` to `LIB_SOURCES` if not inline-only |

## Pitfalls

1. **Anonymous namespace collisions**: Both `.cpp` files currently define `writeAll` etc. in anonymous namespaces. Moving to a shared header requires choosing a visible namespace (e.g. `agentc::pipe_io`) and updating all call sites. Watch for ADL surprises.

2. **`-Werror` and unused fields**: The coordinator side does not use `BoundSymbol::address`. If the compiler warns about unused members in a struct, this is not a problem (struct members don't trigger unused warnings), but if any code path assigns and then ignores it, `-Wunused-but-set` could fire. Verify with the full warning set.

3. **EINTR handling variations**: `tcc_runtime.cpp` and `tcc_worker_native.cpp` handle EINTR in `writeAll`/`readAll`; `edict_worker_runtime.cpp` does NOT. The shared header should include EINTR retry (the TCC version). Do not change `edict_worker_runtime.cpp` in this goal.

4. **Inline vs .cpp**: If `pipe_io.h` is inline-only, the linker must deduplicate the symbols (ODR). Since both `libedict.so` and `agentc_tcc_worker_exec` compile these, inline is fine. Alternatively, put implementations in `pipe_io.cpp` compiled into `libedict.so` and link the worker exec against it (already done for other symbols).

5. **` BoundSymbol` serialization**: `writeBoundSymbols` in both files serializes `name`, `declaration`, `origin` but NOT `address`. The shared struct adds `address` as a worker-only field. Ensure `writeBoundSymbols` and `readBoundSymbols` continue to serialize only the first 3 fields, ignoring `address`.

6. **Test adapter library**: `tcc_test_adapter.cpp` is unaffected by this refactoring — it defines its own `extern "C"` functions and does not use `BoundSymbol` or IPC primitives.

## Verification Checklist

- [x] `edict/pipe_io.h` created with shared IPC primitives
- [x] `tcc_runtime.cpp` and `tcc_worker_native.cpp` include `pipe_io.h` and contain no local IPC definitions
- [x] `BoundSymbol` and TCC constants defined in exactly one location (`tcc_shared.h`)
- [x] Symbol resolution unified into a single function (`resolveSymbolOrigin` in `tcc_shared.h`)
- [x] `allowProcessSymbol` / `allowLibrarySymbol` delegate to a shared helper (`Impl::allowSymbol`)
- [x] Job error envelope helper replaces manual field-copying (`jobErrorEnvelope` in `tcc_runtime.cpp`)
- [x] VM opcode dispatch boilerplate reduced (`dispatchHandleOp` template in `edict_vm_tcc.cpp`)
- [x] `TccRuntimeTest.*` passes 12/12
- [x] Full `edict_tests` passes 210/210
- [x] `build-no-tcc` build succeeds
- [x] No new compiler warnings under `-Wall -Wextra -Werror`

## Progress

### 2026-07-01
- Goal created from code review of the TCC integration layer (G112–G116).
- Identified 8 categories of duplication across `tcc_runtime.cpp`, `tcc_worker_native.cpp`, `edict_vm_tcc.cpp`, and pre-existing IPC code in `edict_worker_runtime.cpp` / `cartographer/service.cpp`.

### 2026-07-01 (completion)
- Created `edict/pipe_io.h`: inline `writeAll`/`readAll`/`writeString`/`readString`/`writeStringVector`/`readStringVector` under `agentc::edict::tcc` namespace; full EINTR retry; no dependencies beyond stdlib + `<unistd.h>`.
- Created `edict/tcc_shared.h`: canonical `BoundSymbol` struct (with `address` field), `kProcessOrigin`/`kModeCompile`/`kModeRun` constants, unified `resolveSymbolOrigin` (validation-only when `outAddress=nullptr`, resolution when non-null), and `closeLibraryHandles`.
- Rewrote `tcc_runtime.cpp`: includes `pipe_io.h` and `tcc_shared.h`; removed all local duplicates; `Impl::allowSymbol` helper eliminates `allowProcessSymbol`/`allowLibrarySymbol` redundancy; `jobErrorEnvelope` free function stamps all 3 failure paths (timeout, cancel, worker-exit-without-result).
- Rewrote `tcc_worker_native.cpp`: includes `pipe_io.h` and `tcc_shared.h`; removed all local duplicates including old `resolveSymbolSpecInCurrentProcess`, `closeDynamicLibraryHandles`, `kProcessOrigin`, and local `BoundSymbol`.
- Rewrote `edict_vm_tcc.cpp`: added `dispatchHandleOp` function template; `op_TCC_SYMBOLS`, `op_TCC_DROP`, `op_TCC_STATUS`, `op_TCC_COLLECT`, `op_TCC_CANCEL` each collapse to a single `dispatchHandleOp` call.
- Verified: `TccRuntimeTest.*` 12/12, `edict_tests` 210/210, `build-no-tcc` clean — zero new warnings.
