# Goal: G074 — Real-time FFI Token Streaming

**Work Product**: 🔗[WP — G074 Real-time FFI Token Streaming](../../WorkProducts/WP-G074-RealtimeFFITokenStreaming-2026-05-11/index.md)  
**Status**: COMPLETE  
**Created**: 2026-05-09  
**Reassessed**: 2026-05-11  
**Completed**: 2026-05-11  

## Objective
Extend the FFI and Edict VM to support real-time token streaming using an **Actor Model / Decoupled Ghost Queue** architecture. The in-VM agent loop will spawn ephemeral, stateless background FFI requests and explicitly synchronize their results back into the native Listree memory space at its own discretion.

## Design & Rationale
We are bringing the Actor Model / CSP to the Listree graph database. Because Listree uses an extremely fast, mmap-backed single-threaded slab allocator, mutating it from background threads would risk memory corruption without heavy locking. 

**The Decoupled Ghost Queue Architecture:**
1. **Ephemeral Waiter**: When Edict requests an LLM stream (`agentc_call_stream`), C++ spawns a detached background thread. This thread is entirely stateless beyond the single request. It streams tokens into a standard thread-safe C++ `std::queue`. It *never* touches Listree memory.
2. **Explicit Synchronization**: Edict retains full control over its cognitive cycle. It calls `agentc_stream_sync` with a runtime handle plus `stream_id`. The C++ host drains the decoupled queue and returns a structured sync envelope; Edict/provider code then mutates its mailbox/provider fields on the VM/main thread.
3. **Resilience & Persistence**: Because the background threads contain no critical VM state, discarding them during a save/restore cycle is perfectly safe. If the VM is killed and restored from the mmap slab, the transient C++ threads vanish. Edict simply wakes up, sees a stuck `mailbox`, and handles the timeout/retry purely in logical script space.

## Architecture Blueprint
- **`StreamManager` (C++)**: Maps a `stream_id` to a thread-safe `std::queue<std::string>`.
- **`agentc_call_stream` (FFI)**: Spawns the LLM network thread, registers it with `StreamManager`, returns `stream_id`.
- **`agentc_stream_sync` (FFI)**: `( runtime_handle stream_id -- sync_envelope )` Drains pending tokens from `StreamManager` and returns a structured JSON/Listree envelope with `tokens`, `ok`, `complete`, and `error`. Edict/provider code then mutates its mailbox/provider fields on the VM/main thread.
- **Edict Loop**: A non-blocking `yield`/poll loop or provider-level `stream_sync` thunk actively polls `agentc_stream_sync` and routes tokens to the UI natively.

## Acceptance Criteria
- [x] Implement thread-safe `StreamManager` in C++ host.
- [x] Bind `agentc_call_stream` to launch a detached LLM request thread.
- [x] Bind `agentc_stream_sync` to safely synchronize stream data back onto the VM/main thread.
- [x] Add provider-level Edict stream start/sync helpers that demonstrate a non-blocking asynchronous stream loop surface.

## Reassessment — 2026-05-11
Streaming remained a valid architecture direction, and the Decoupled Ghost Queue design stayed the preferred safety model for mmap/Listree integrity. After G079 landed the first tool/action surface, G074 was implemented as the next infrastructure slice.

## Implementation Notes — 2026-05-11
Detailed implementation and usage guide: 🔗[WP — G074 Real-time FFI Token Streaming](../../WorkProducts/WP-G074-RealtimeFFITokenStreaming-2026-05-11/index.md).

- `Runtime` now initializes a `StreamManager` and `stream_request_json(...)` creates a stream id plus a detached background provider worker.
- The background worker owns only ordinary C++ state and pushes provider text deltas into `StreamManager`; it never mutates Listree/VM memory.
- `agentc_runtime_stream_sync_json(...)` now returns a structured JSON envelope:
  - `ok`: `["ok"]` or `[]`
  - `stream_id`
  - `tokens`
  - `complete`: `["complete"]` or `[]`
  - `error`
- `agentc.edict` now defines `agentc_call_stream !` and `agentc_stream_sync !` wrappers.
- `llm.edict` provider objects now include `stream_start` and `stream_sync` thunks plus stream state fields (`stream_id`, `stream_runtime`, `stream_last`, `stream_complete`, etc.).
- Added inexpensive Google/Gemma preset aliases in `llm.edict`: `gemma-4-31b-it` and `google-gemma-4-31b-it`.
- `google_provider.cpp` now uses the Google `streamGenerateContent?alt=sse` endpoint and parses SSE chunks. Thought parts are skipped so Gemma thinking does not pollute assistant text. Debug logging that exposed request URLs/raw responses was removed.
- The mock runtime now implements stream request/sync functions so stream wrapper tests stay offline and deterministic.

## Validation — 2026-05-11
- Focused stream tests passed:
  - `AgentRuntimeAbiTest.StreamApiReturnsHandlesAndSynchronizes`
  - `EdictAgentcModuleTest.StreamWrapperSpawnsAndSynchronizes`
  - `EdictLlmModuleTest.ProviderStreamStartAndSyncUsesRuntimeStreamApi`
  - `EdictStatefulLoopTest.StreamTurnActorLoopExecutesAndSynchronizes`
  - `StreamManagerTest.*`
- Broader targeted suite passed:
  - `./build/cpp-agent/cpp_agent_tests --gtest_filter='AgentRuntimeAbiTest.*:EdictAgentcModuleTest.*:EdictLlmModuleTest.*:EdictStatefulLoopTest.*:EdictAgentRootTest.*:AgentRootVmOpsTest.*:StreamManagerTest.*'` — 28/28 passed.
  - `./build/edict/edict_tests --gtest_filter='EdictVM.*'` — 22/22 passed.
- Live low-cost Google/Gemma smoke test succeeded with `gemma-4-31b-it`: started a provider stream through `llm.init([gemma-4-31b-it])`, synchronized after a short wait, and received a structured sync envelope whose `tokens` were `ok` and whose `complete` sentinel was set.
