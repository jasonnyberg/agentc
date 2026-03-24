# Edict VM Multithreading Plan

## Objective

Add multithreaded Edict execution by combining imported pthread-backed helpers with the existing FFI callback/closure system, while keeping the VM itself single-owner and making shared mutable Listree state explicit through protected cells rather than unsynchronized live graph sharing.

## Current Baseline

### What already exists

- Edict can capture a thunk plus root scope as a continuation object and turn it into a native callback closure.
- `closure_thunk(...)` already executes that callback in a fresh `EdictVM`, binding callback arguments into a callback scope.
- The FFI already supports `ltv` passthrough, so thread helpers can accept or return Listree values without inventing a second representation.

### What is missing

- No thread-spawn helper exists today.
- No VM ownership guard prevents users from accidentally treating one `EdictVM` as concurrently reusable.
- No Listree or allocator synchronization exists for arbitrary shared mutation.
- Current mutable Listree operations (`find(insert=true)`, `put`, `remove`, `pin`, `setFlags`, etc.) assume single-threaded access.

## Core Design Decision

### First threading slice: fresh VM per thread + explicit protected cells

The first implementation should **not** attempt to make all live Listree nodes or the slab allocator freely shareable across threads.

Instead:

1. A new pthread runs a thunk in a **fresh `EdictVM`**.
2. Any data intentionally shared across threads crosses through an explicit protected-value abstraction.
3. That abstraction uses **coarse locking plus snapshot/replace semantics**, not fine-grained locking on every Listree node.

This is the smallest model that is both useful and believable.

## Why This First Model Is The Right One

### Why not share one VM across threads?

- `EdictVM` owns mutable stacks, execution pointers, cursor state, transaction checkpoints, rewrite state, and error fields.
- A single VM instance is not remotely close to thread-safe today.
- Making one VM concurrently executable would force synchronization across the entire interpreter hot path before there is any proven threaded use case.

### Why not lock every Listree node?

- The allocator, refcounting, list/tree mutation, and pinning model are all currently unsynchronized.
- Fine-grained locking would spread concurrency concerns through nearly every container and runtime primitive.
- It would also create hard-to-reason-about deadlock and lock-order problems early.

### Why protected cells?

- The thread boundary becomes explicit and reviewable.
- Shared mutation is intentionally narrowed to a small set of APIs.
- The cell implementation can use one mutex per shared object and avoid pretending the whole runtime is safe for arbitrary concurrent pointer sharing.

## Proposed First Safety Model

### Protected shared value

Introduce a small pthread-backed helper capability, conceptually:

```c
typedef struct agentc_shared_value agentc_shared_value;

agentc_shared_value* agentc_shared_create_ltv(LTV initial);
void agentc_shared_destroy(agentc_shared_value* cell);
LTV agentc_shared_read_ltv(agentc_shared_value* cell);
int agentc_shared_write_ltv(agentc_shared_value* cell, LTV replacement);
```

Semantics:

- `create`: stores a canonical owned value inside the cell.
- `read`: returns a safe snapshot/copy for the caller thread.
- `write`: atomically replaces the stored value under a mutex.
- No caller ever gets permission to mutate the cell's live internal value in place.

### Thread entry

Expose a helper library that uses pthread internally but takes an FFI callback entrypoint, for example:

```c
typedef struct agentc_thread_handle agentc_thread_handle;

agentc_thread_handle* agentc_thread_spawn_ltv(LTV (*entry)(LTV), LTV arg);
LTV agentc_thread_join_ltv(agentc_thread_handle* handle);
void agentc_thread_detach(agentc_thread_handle* handle);
void agentc_thread_destroy(agentc_thread_handle* handle);
```

The important part is not the exact function names; it is the ownership model:

- Edict passes a thunk through the existing callback-signature machinery.
- The helper starts a pthread.
- The callback executes inside a fresh `EdictVM` via the already existing closure callback path.
- The thread returns an `ltv` result at join time.

## Runtime Ownership Model

### Thread-local VM state

Each thread gets:

- its own `EdictVM`;
- its own execution stacks and interpreter state;
- its own temporary working values;
- its own callback-scope bindings.

### Cross-thread state

Only these should cross thread boundaries in the first slice:

- copied `ltv` arguments/results;
- explicit `agentc_shared_value*` handles;
- imported function handles/callback pointers managed by the helper library.

### Not allowed in the first slice

- two threads mutating the same live Listree subtree directly;
- one thread holding a raw `CPtr<ListreeValue>` into another thread's mutable working state and editing it in place;
- concurrent use of one `EdictVM` instance.

## Interaction With Existing Callback Machinery

This plan intentionally reuses the code already proven in the callback path:

- `buildClosureValue(...)` already captures `THUNK` + `ROOT` + `SIGNATURE`.
- `closure_thunk(...)` already constructs a fresh VM and evaluates the thunk.

The thread helper should leverage that path instead of inventing a second “execute thunk” subsystem.

That means the first threading milestone is mostly:

1. a pthread helper library,
2. protected shared-value cells,
3. imported tests proving the callback thunk works in a new thread.

## Example User-Facing Story

### Example 1: background computation without shared mutation

```edict
[logicffi.agentc_thread_spawn_ltv !] @thread_spawn
[logicffi.agentc_thread_join_ltv !] @thread_join

[ ARG0 ] @identity_worker
identity_worker 'hello thread_spawn @t
t thread_join !
```

Expected effect:

- a pthread is launched,
- the worker thunk runs in a fresh VM,
- join returns `'hello`.

### Example 2: explicit shared state with snapshot/replace

```edict
[threadffi.agentc_shared_create_ltv !] @shared_create
[threadffi.agentc_shared_read_ltv !] @shared_read
[threadffi.agentc_shared_write_ltv !] @shared_write

'0 shared_create @counter

[
  ARG0 shared_read ! @snap
  # mutate local snapshot here
  '1 ARG0 shared_write !
] @worker
```

Expected effect:

- the worker does not mutate a live shared Listree node directly,
- it reads a snapshot and publishes a replacement under lock.

### Example 3: continuation as callback thunk

```edict
{
  "return_type": "ltv",
  "children": {
    "p0": {"kind": "Parameter", "type": "ltv"}
  }
} @thread_sig

thread_sig [ ARG0 ] ffi_closure
```

The thread helper should accept that closure pointer as its callback entry, so thread execution uses the same callback machinery already exercised elsewhere in the runtime.

## Recommended Implementation Slices

### Slice A - pthread helper proof with no shared mutable state

- Build a small helper library, likely under `demo/` or a new runtime helper area, that wraps `pthread_create` / `pthread_join`.
- Accept an FFI callback pointer and an `ltv` argument.
- Add imported test coverage proving a thunk runs in a new thread and returns an `ltv` result.

### Slice B - protected shared-value cells

- Add `agentc_shared_create_ltv`, `agentc_shared_read_ltv`, `agentc_shared_write_ltv`.
- Store canonical owned `LTV` values inside a mutex-protected cell.
- Read returns a copy/snapshot; write replaces atomically.

### Slice C - Edict-side wrappers

- Build ordinary Edict wrappers for thread spawn/join and shared-cell access.
- Keep the VM free of new thread opcodes.
- Prefer imported capability plumbing plus wrapper ergonomics.

### Slice D - hardening and limits

- Add tests that intentionally attempt overlapping access through the protected path.
- Add negative/documentation coverage clarifying that arbitrary live subtree sharing remains unsupported.

## Code Areas Likely To Change

- New helper library, likely a small C/C++ pthread wrapper with Cartographer-friendly header.
- `edict/tests/callback_test.cpp` for imported callback-thread coverage.
- Potentially `cartographer/tests/` for a helper import header.
- Documentation in `README.md` or the language reference once the first slice is implemented.

## Risks

### Risk 1: accidental sharing of unsafe live values

Mitigation: do not expose “borrow this live `CPtr` to another thread and mutate it” APIs.

### Risk 2: confusion between copied values and protected shared values

Mitigation: make shared cells distinct opaque handles with explicit read/write entrypoints.

### Risk 3: callback/thread lifetime bugs

Mitigation: keep spawn/join ownership explicit and test join, destroy, and callback cleanup paths carefully.

### Risk 4: overreaching on allocator-wide thread safety too early

Mitigation: keep allocator/global Listree internals single-thread-assumed for now; synchronize only inside the shared-cell helper boundary.

## Verification Plan

- Imported callback-thread smoke test: thunk runs in a new pthread and returns a value.
- Shared-cell test: two worker thunks serialize replacement through one protected cell without corrupting the value.
- Negative/documentation coverage: direct concurrent mutation of arbitrary live subtrees remains unsupported and is not exposed by helper APIs.
- Full `ctest` remains green.

## Recommended First Acceptance Slice

The smallest believable first slice is:

1. implement pthread-backed thunk spawn/join with no shared mutation,
2. prove it through imported callback tests,
3. then add one mutex-backed protected shared-value cell,
4. only after that consider richer wrapper ergonomics.

## Bottom Line

The first multithreading goal should not be “make the whole VM thread-safe.” It should be: reuse the existing callback/continuation machinery to run a thunk in a fresh thread-local VM, and make cross-thread mutable state explicit through protected snapshot/replace cells. That gives AgentC a credible threaded execution story without pretending the current Listree/allocator substrate is already safe for arbitrary concurrent mutation.
