# WP — G074 Real-time FFI Token Streaming

**Status**: Active reference  
**Date**: 2026-05-11  
**Scope**: LOCAL — AgentC / J3  
**Granularity**: implementation summary + usage guide  
**Related Goal**: 🔗[G074 — Real-time FFI Token Streaming](../../Goals/G074-RealtimeFFITokenStreaming/index.md)

## Purpose

This work product explains the G074 implementation: the decoupled ghost-queue streaming path for AgentC/Edict, the capabilities it adds, and the current practical Edict usage patterns.

G074's central rule is:

> Background provider workers may wait on network streams and push text into ordinary C++ queues, but they must not mutate Listree or VM state. Edict explicitly synchronizes queued data back on the VM/main thread.

## What Changed

### Runtime / C++ substrate

- `Runtime` now owns a `std::shared_ptr<StreamManager>`.
- `Runtime::stream_request_json(...)` now:
  1. validates/builds a normal runtime request,
  2. creates a stream id,
  3. launches a detached provider worker thread,
  4. wires provider `EvTextDelta` events into `StreamManager::pushToken(...)`,
  5. marks the stream complete or errored in `StreamManager`.
- Background workers own only ordinary C++ state and provider/network resources.
- `StreamManager` remains the handoff point between background provider execution and foreground VM synchronization.

### C ABI

The runtime C ABI now exposes a structured stream-sync surface:

- `agentc_runtime_stream_request_json(runtime, request_json) -> stream_id`
- `agentc_runtime_stream_sync_json(runtime, stream_id) -> sync_json`

`agentc_runtime_stream_sync_json(...)` returns JSON shaped like:

```json
{
  "ok": ["ok"],
  "stream_id": "stream_1",
  "tokens": "text received since last sync",
  "complete": ["complete"],
  "error": null
}
```

Failure uses `"ok": []` and an `error` object.

### Edict wrapper layer

`cpp-agent/edict/modules/agentc.edict` now defines:

```edict
agentc_call_stream!   -- ( runtime_handle request_value -- stream_id )
agentc_stream_sync!   -- ( runtime_handle stream_id -- sync_value )
```

These convert Edict values to JSON/C strings, call the runtime C ABI, convert the returned JSON string back into Listree data, and free native strings.

### Provider object surface

`cpp-agent/edict/modules/llm.edict` provider objects now carry streaming state and thunks:

Fields:

- `provider.stream_id`
- `provider.stream_runtime`
- `provider.stream_last`
- `provider.stream_complete`
- `provider.stream_text` — reserved for accumulation policy; first slice primarily uses `stream_last.tokens`

Methods/thunks:

```edict
provider < [prompt] stream_start! > / /
provider < stream_sync! > / /
```

`stream_start` appends the user prompt to provider conversation, builds the canonical request, creates a runtime handle, starts the background stream, and records `stream_id` / `stream_runtime`.

`stream_sync` drains pending queued text into `provider.stream_last`. It mirrors the latest drained token batch into `provider.assistant_text`, records `provider.stream_complete`, and destroys the stream runtime when completion is observed.

### Google provider

`cpp-agent/runtime/providers/google/google_provider.cpp` now uses Google's streaming endpoint:

```text
/v1beta/models/{model}:streamGenerateContent?alt=sse
```

It parses SSE data events and emits runtime text deltas. It skips Google/Gemma `thought` parts so hidden/visible reasoning chunks do not pollute assistant text.

Debug logging that printed request URLs/raw responses was removed to avoid leaking API keys.

### Inexpensive Google/Gemma preset

`llm.edict` now includes low-cost Google/Gemma aliases:

- `gemma-4-31b-it`
- `google-gemma-4-31b-it`

Both configure the Google runtime provider with model `gemma-4-31b-it`.

## Capabilities Afforded

G074 adds the first practical foundation for real-time AgentC streaming:

1. **Non-blocking provider starts** — Edict can start a provider request and regain control immediately.
2. **Safe token handoff** — provider threads push text into C++ queues; Edict syncs on the VM/main thread.
3. **Streaming UI foundation** — callers can poll `agentc_stream_sync!` and forward token batches to a UI, terminal, mailbox, or conversation state.
4. **Persistence-safe semantics** — in-flight native workers are transient. Durable Edict state can record a stream/mailbox status and decide retry/timeout after restore.
5. **Provider-surface integration** — `llm.init(...)` providers now expose `stream_start` / `stream_sync`, so streaming follows the same stable provider-object pattern as normal requests and tools.
6. **Cheap live validation path** — `gemma-4-31b-it` can exercise the streaming path without using expensive Codex models.

## How To Use It

### Provider-level streaming

Use the curated launcher and a provider preset:

```edict
llm.init([gemma-4-31b-it]) @provider
provider < [Reply exactly ok] stream_start! > / /
```

Later, after yielding, doing other work, or waiting briefly:

```edict
provider < stream_sync! > / /
provider.stream_last.tokens print
```

Inspect completion:

```edict
provider.stream_complete to_json! print
```

A completed stream has:

```json
["complete"]
```

### Direct wrapper usage

If you already have a runtime handle and request value:

```edict
runtime request agentc_call_stream! @stream_id
runtime stream_id agentc_stream_sync! @sync
sync.tokens print
```

`sync` is a normal Edict/Listree object created from the runtime JSON envelope.

### Live smoke command

The implementation was live-validated with the inexpensive Google/Gemma preset:

```bash
EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.init([gemma-4-31b-it]) @provider provider < [Reply with exactly two lowercase letters: ok] stream_start! > / / provider < [sleep 15] tools.shell! @sleep_result > / / provider < stream_runtime stream_id agentc_stream_sync! @manual_sync > / / provider.manual_sync to_json! print provider < stream_runtime agentc_destroy! > / /'
```

Observed result:

```json
{"complete":["complete"],"error":null,"ok":["ok"],"stream_id":"stream_1","tokens":"ok"}
```

The `tools.shell` sleep is only a convenient current wait mechanism. A future UI loop should poll/yield rather than shelling out to sleep.

## Current Sharp Edges / Follow-up Work

- `provider.stream_sync` currently records the **latest drained batch** in `provider.stream_last.tokens` and mirrors that batch into `provider.assistant_text`; full accumulation policy should be explicit in a future loop/context-management slice.
- The current provider-level stream surface is a start/sync primitive, not yet a full polished UI loop.
- Restore behavior for in-flight streams should be represented as stale logical state that Edict can timeout/retry; background native workers are intentionally not durable.
- Google streaming is implemented through SSE; other providers use the same runtime event abstraction but may need provider-specific refinements.
- A future tool/context loop should combine streaming with explicit conversation update policy once the stream is complete.

## Validation

Focused and broader tests passed:

```bash
./build/cpp-agent/cpp_agent_tests --gtest_filter='AgentRuntimeAbiTest.*:EdictAgentcModuleTest.*:EdictLlmModuleTest.*:EdictStatefulLoopTest.*:EdictAgentRootTest.*:AgentRootVmOpsTest.*:StreamManagerTest.*'
# 28/28 passed

./build/edict/edict_tests --gtest_filter='EdictVM.*'
# 22/22 passed
```

Live Google/Gemma validation succeeded with `gemma-4-31b-it` returning `ok` through the streaming sync envelope.
