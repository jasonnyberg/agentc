# Goal: G074 — Real-time FFI Token Streaming

**Status**: IN PROGRESS  
**Created**: 2026-05-09  

## Objective
Extend the FFI and Edict VM to support real-time token streaming using an **Actor Model / Decoupled Ghost Queue** architecture. The in-VM agent loop will spawn ephemeral, stateless background FFI requests and explicitly synchronize their results back into the native Listree memory space at its own discretion.

## Design & Rationale
We are bringing the Actor Model / CSP to the Listree graph database. Because Listree uses an extremely fast, mmap-backed single-threaded slab allocator, mutating it from background threads would risk memory corruption without heavy locking. 

**The Decoupled Ghost Queue Architecture:**
1. **Ephemeral Waiter**: When Edict requests an LLM stream (`agentc_call_stream`), C++ spawns a detached background thread. This thread is entirely stateless beyond the single request. It streams tokens into a standard thread-safe C++ `std::queue`. It *never* touches Listree memory.
2. **Explicit Synchronization**: Edict retains full control over its cognitive cycle. It calls a new built-in word `agentc_stream_sync` passing a `stream_id` and a target Listree `mailbox` object. The C++ host drains the decoupled queue on the main thread and safely mutates the Edict mailbox natively.
3. **Resilience & Persistence**: Because the background threads contain no critical VM state, discarding them during a save/restore cycle is perfectly safe. If the VM is killed and restored from the mmap slab, the transient C++ threads vanish. Edict simply wakes up, sees a stuck `mailbox`, and handles the timeout/retry purely in logical script space.

## Architecture Blueprint
- **`StreamManager` (C++)**: Maps a `stream_id` to a thread-safe `std::queue<std::string>`.
- **`agentc_call_stream` (FFI)**: Spawns the LLM network thread, registers it with `StreamManager`, returns `stream_id`.
- **`agentc_stream_sync` (FFI)**: `( stream_id mailbox -- )` Drains pending tokens from `StreamManager` and appends them to `mailbox.new_tokens`, updating `mailbox.status` to `"complete"` when finished.
- **Edict Loop**: A non-blocking `yield` loop that actively polls `agentc_stream_sync` and routes tokens to the UI natively.

## Acceptance Criteria
- [ ] Implement thread-safe `StreamManager` in C++ host.
- [ ] Bind `agentc_call_stream` to launch a detached LLM request thread.
- [ ] Bind `agentc_stream_sync` to safely mutate the Listree mailbox on the main thread.
- [ ] Update `agentc_stateful_loop.edict` to demonstrate a real-time, non-blocking asynchronous stream loop.