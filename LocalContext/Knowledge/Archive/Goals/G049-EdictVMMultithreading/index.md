# G049 - Edict VM Multithreading

## Status: COMPLETE (RETIRED INTO G052)

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Related runtime goal: 🔗[`G046-ContinuationBasedSpeculation`](../G046-ContinuationBasedSpeculation/index.md)
- Related FFI precedent: 🔗[`G018-FfiLtvPassthrough`](../G018-FfiLtvPassthrough/index.md)

## Goal

Introduce multithreaded Edict execution through imported FFI callbacks and `pthread`, so a new native thread can invoke a captured Edict continuation/thunk in a fresh `EdictVM`, while shared Listree data crosses the thread boundary only through an explicit protection mechanism that prevents unsynchronized concurrent mutation.

## Why This Matters

- The runtime already knows how to capture a thunk plus root scope and execute it in a fresh VM for FFI callbacks; threading is a natural extension of that capability.
- Background thunk execution would make AgentC more useful for sidecar work, asynchronous capability adapters, and future sub-agent/process orchestration.
- The hard part is not starting a thread; it is defining a safe ownership model for mutable Listree data so the runtime does not race on shared slab-backed values.
- A clean first threading slice would expand capability without turning the VM itself into a free-for-all shared-memory object.

## Evidence Snapshot

**Claim**: The runtime already has the ingredients for threaded thunk execution, but not for safe shared mutation.
**Evidence**:
- Primary: `edict/edict_vm.cpp:2671` `closure_thunk(...)` already creates a fresh `EdictVM` from captured continuation/root data and executes the thunk through the normal VM path.
- Primary: `edict/edict_vm.cpp:2708` `buildClosureValue(...)` already packages `THUNK`, `ROOT`, and `SIGNATURE` into a continuation object and hands it to libffi closure creation.
- Primary: `cartographer/ffi.cpp:224` shows `ltv` passthrough already exists in the FFI layer, so thread helper APIs can move Listree values across a C boundary without inventing a second value format.
- Primary: `listree/listree.h:228` and `listree/listree.h:233` show direct mutable operations (`pin`, `unpin`, `find(insert=true)`, `put`, `get`, `setFlags`) with no synchronization or ownership guard.
- Primary: `edict/edict_vm.h` exposes mutable shared VM resource stacks and execution fields with no thread ownership markers or locks.
**Confidence**: 95%

## Expected System Effect

- A native helper library can spawn a `pthread` that invokes an Edict thunk through the existing callback/closure path.
- Each thread runs its own fresh `EdictVM`; the first implementation does **not** permit concurrent execution against the same live VM instance.
- Shared mutable state becomes explicit: threads exchange or update protected cells/snapshots rather than mutating arbitrary live Listree nodes concurrently.

## Work Product

- Detailed plan: 🔗[`EdictVMMultithreadingPlan-2026-03-23.md`](../../WorkProducts/EdictVMMultithreadingPlan-2026-03-23.md)

## Implementation Checklist

- [x] Confirm the first concurrency boundary: fresh VM per thread, no concurrent reuse of one `EdictVM` instance.
- [x] Design the protected shared-value mechanism for cross-thread Listree access.
- [x] Define a small pthread-backed helper ABI that uses callback signatures rather than VM-owned thread opcodes.
- [x] Add imported Edict-side coverage for spawn, join, and protected shared-value operations.
- [x] Validate that concurrent mutation only occurs through the protected-value path.
- [x] Document the threading model and its limitations.
- [x] Trim the first-slice helper surface to the direct `ltv` thread entry/join path plus protected shared-value cells.

## Progress Notes

- 2026-03-23: Added `cartographer/tests/libagentthreads_poc.h` / `.cpp` with imported pthread-backed helper APIs for `agentc_thread_spawn_ltv`, `agentc_thread_join_ltv`, mutex-protected shared-value cells, and experimental status-returning thread entrypoints.
- 2026-03-23: Hardened callback execution so `closure_thunk(...)` uses a copied captured `ROOT` and preloads imported libraries referenced from copied callback scope metadata before executing worker thunks.
- 2026-03-23: Focused G049 tests now prove three concrete behaviors in isolation: direct `ltv` thread spawn/join works, shared-cell snapshot isolation works, and threaded shared-cell update works.
- 2026-03-23: Corrected raw ABI `ltv` handle encode/decode in `cartographer/tests/libagentthreads_poc.cpp`, cleaned the threaded spawn-result test stack setup, and rebuilt both `agentthreads_poc` and `edict_tests` together. Regression coverage now passes, the full `CallbackTest.*` suite is green, and `ctest --output-on-failure` is back to `7/7` passing.
- 2026-03-23: Remaining work is now documentation/cleanup: explicitly record the first-slice threading limits and decide whether the auxiliary status-returning thread-entry API should remain in the helper surface.
- 2026-04-03: G050 is complete. The thread-runtime path is now green in repeated focused runs, standalone `./build/edict/edict_tests`, and full `ctest` after helper dependency wiring plus atomic slab-handle retain fixes.
- 2026-04-03: Documented the landed first-slice threading model in `README.md` and `LocalContext/Knowledge/WorkProducts/edict_language_reference.md`: fresh VM per thread, copied `ltv` arguments/results, explicit protected shared-value cells for mutable cross-thread state, and no supported arbitrary live-graph sharing or shared-VM execution.
- 2026-04-03: Evaluated G051 (`Cursor`-visited read-only marking) as a possible stronger concurrency boundary. Current recommendation is not to adopt it as the primary G049 model because mutation is not cursor-exclusive, visited scope is ambiguous, and overlapping cursor lifetimes would require lease bookkeeping anyway. Protected shared-value cells remain the recommended first-slice boundary.
- 2026-04-03: Audited the helper/mutation surface and confirmed the supported cross-thread mutable path remains the mutex-protected shared-cell API. Direct thread spawn/join still snapshots `ltv` values at the boundary, while the auxiliary status-returning thread entry path was removed because it was only supporting one test and no longer adds unique first-slice capability.
- 2026-04-03: Added a small misuse-detection hardening step in the pthread helper: iterator/cursor-valued `ltv` handles are now rejected at the thread/shared-cell transfer boundary rather than being copied across threads. Added `PoCTest.ThreadHelperRejectsIteratorValTransfers`, and full `cartographer_tests` / `ctest` remain green.
- 2026-04-04: Closed the remaining protected-value validation item. `CallbackTest.ImportResolvedThreadRuntimeDirectCapturedMutationDoesNotLeakAcrossThreads` proves that direct mutation of captured root state inside a worker thread does not leak back to the caller VM, while the shared-cell tests remain the only positive mutable coordination path.

## Current Validation Readout

- `agentc_thread_spawn_ltv(...)` snapshots the incoming `ltv` argument before worker launch.
- `agentc_thread_join_ltv(...)` returns a fresh snapshot of the worker result rather than exposing a live mutable value.
- `agentc_shared_create_ltv(...)`, `agentc_shared_read_ltv(...)`, and `agentc_shared_write_ltv(...)` remain the only supported mutable cross-thread coordination surface in the helper library.
- The prior `agentc_thread_spawn_status(...)` / `agentc_thread_join_status(...)` path is now removed; the shared-cell update coverage uses the ordinary `ltv` callback path instead.
- Iterator/cursor-valued `ltv` handles are now explicitly treated as unsafe for thread/shared-cell transfer; the helper snapshots them as null instead of attempting to move a live cursor-backed object across threads.
- `CallbackTest.ImportResolvedThreadRuntimeDirectCapturedMutationDoesNotLeakAcrossThreads` proves that mutating captured root state inside the worker does not act as a shared mutable channel; positive mutable coordination still requires `agentc_shared_*` APIs.
- Validation after the trim is green: focused thread-runtime callback tests pass, `./build/edict/edict_tests` passes (`86/86`), and `ctest --test-dir build --output-on-failure` passes (`7/7`).

## Scope

### In Scope

- Imported thread spawning through a pthread-backed helper library.
- Reuse of existing Edict FFI callback/continuation machinery for thread entry.
- A first explicit protection mechanism for shared Listree data.
- Focused tests covering callback-thread launch, join, and protected shared-value access.

### Out of Scope

- Concurrent execution on one live `EdictVM` instance.
- Fine-grained locking of all Listree nodes and allocator structures.
- Lock-free data structures.
- Fully general shared-memory mutation of arbitrary live `CPtr<ListreeValue>` graphs.

## Proposed Phases

1. Define the threading ownership model and protected-value boundary.
2. Add a small pthread-backed helper library with callback-based spawn/join entrypoints.
3. Add protected shared-value cells with coarse locking plus snapshot/replace semantics.
4. Prove imported Edict-side threaded thunk execution with focused tests.
5. Document limits and identify whether a later finer-grained concurrency model is warranted.

## Success Criteria

- An imported helper can launch a new pthread that invokes an Edict continuation/thunk through the existing FFI closure mechanism.
- The thread runs in a fresh `EdictVM` rather than sharing one mutable VM instance.
- Shared mutable Listree state is only accessed through an explicit protection mechanism.
- Focused concurrency tests pass, and the full suite remains green.

## Acceptance Signals For Implementation Kickoff

- The plan chooses one concrete first safety model rather than leaving concurrency semantics open-ended.
- The spawn/join API is small enough to test through normal Cartographer resolve/import.
- The design is explicit about what is *not* yet safe, especially direct concurrent mutation of arbitrary live Listree nodes.

## Navigation Guide for Agents

- Re-read `edict/edict_vm.cpp` around `closure_thunk(...)` and `buildClosureValue(...)` before implementation.
- Treat FFI callback reuse as the enabling mechanism and shared-data protection as the real design decision.
- Keep the first slice conservative: explicit protected cells beat pretending the current VM/Listree substrate is already thread-safe.

## Retirement Note

- 2026-04-04: Retired as a standalone active goal in favor of 🔗[`G052-RuntimeBoundaryHardening`](../G052-RuntimeBoundaryHardening/index.md). The first-slice threading model is implemented and validated; remaining work is boundary hardening and cleanup rather than new multithreading feature design.
