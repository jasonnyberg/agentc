# G049 - Edict VM Multithreading

## Status: PLANNED

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

- [ ] Confirm the first concurrency boundary: fresh VM per thread, no concurrent reuse of one `EdictVM` instance.
- [ ] Design the protected shared-value mechanism for cross-thread Listree access.
- [ ] Define a small pthread-backed helper ABI that uses callback signatures rather than VM-owned thread opcodes.
- [ ] Add imported Edict-side coverage for spawn, join, and protected shared-value operations.
- [ ] Validate that concurrent mutation only occurs through the protected-value path.
- [ ] Document the threading model and its limitations.

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
