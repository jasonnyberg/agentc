# Knowledge: Edict Provider Object Surface

**ID**: LOCAL:K031
**Category**: 🔗[J3 AgentC](index.md)
**Tags**: #j3, #edict, #llm, #provider, #agent-loop
**Status**: Active
**Last Referenced**: 2026-05-14

## Overview
The current AgentC LLM surface is centered on stable Edict provider objects created by `llm.init(...)`. This is the primary Edict-resident control-plane seam under 🔗[G078 — Edict-Resident Agent Loop Consolidation](../../Goals/G078-EdictResidentAgentLoopConsolidation/index.md): Edict owns provider configuration objects, conversation state, request construction, tool attachment, stream synchronization state, and first context-management policy while C++ remains responsible for transport, credentials, provider adapters, and native lifecycle.

## Provider Construction
```edict
llm.init([local-qwen]) @provider
```

Useful presets include:
- `local-qwen` / `local`
- `google` / `google-gemini-3-1-pro-preview`
- `gemma-4-31b-it` / `google-gemma-4-31b-it`
- `openai-codex` / `gpt-5.3-codex`

## Reliable Mutation Pattern
Provider methods that mutate provider-owned state should run inside provider scope:

```edict
provider < [prompt text] request! > / /
provider.assistant_text print
```

The first `/` discards the provider context returned by `CTX_POP`; the second discards the method sentinel/result when the caller only needs the provider's updated fields.

## Current Provider-Owned Fields
The `llm.edict` provider object currently carries:
- `preset_name`
- `runtime`
- `conversation`
- `assistant_text`
- `last_response`
- `last_error`
- `request`
- `stream_start`
- `stream_sync`
- `stream_id`
- `stream_runtime`
- `stream_last`
- `stream_text`
- `stream_complete`
- `context_reset`
- `context_inspect`
- `repl`
- `tools`
- imported FFI handles `ext` and `runtimeffi`

## Context Management Slice
🔗[G080 — LLM REPL Context Management](../../Goals/G080-LlmReplContextManagement/index.md) landed the first explicit provider-context management surface:

```edict
provider < context_inspect! > / @summary
summary.messages to_json! print
provider < context_reset! > / /
```

Semantics:
- `context_inspect!` returns a JSON-serializable summary of preset, provider/model, system prompt, messages, assistant text, last response/error, and stream status fields.
- `context_reset!` clears provider conversation history and assistant/error state while preserving the stable provider object and system prompt.
- The first reset/inspect slice does not implement trim/summarize policy; those remain future context-management work.

## Launcher REPL Commands
In launcher-backed `provider.repl()` sessions:
- `/reset` and `/clear` call `provider.context_reset!`.
- `/context` and `/inspect` call `provider.context_inspect!` and print the JSON summary.
- Blank lines are skipped.
- EOF exits cleanly.

The command dispatch path compares raw input strings through `agentc_string_equals!` / `agentc_ext_string_equals_ltv_value`; ordinary chat text is not evaluated as Edict source.

## Tool and Stream Integration
Provider objects also carry:
- `provider.tools` from `agentc_tools` for file read/write/replace and shell helpers.
- `stream_start` / `stream_sync` for ghost-queue provider streaming. Background provider workers do not mutate Listree; Edict synchronizes stream results on the VM/main thread.

## Validated Coverage
The current provider surface is regression-covered by focused cpp-agent tests including:
- `EdictLlmModuleTest.ProviderRequestRunsTurnAndReturnsUpdatedProvider`
- `EdictLlmModuleTest.ProviderRequestSupportsRepeatedTurns`
- `EdictLlmModuleTest.ProviderContextResetClearsConversationInPlace`
- `EdictLlmModuleTest.ProviderReplHandlesMultiplePromptsAndEof`
- `EdictLlmModuleTest.ProviderReplSupportsContextSlashCommands`
- `EdictLlmModuleTest.ProviderStreamStartAndSyncUsesRuntimeStreamApi`
- `EdictLlmModuleTest.LiveGoogleGemmaRequestReturnsOkWhenCredentialsArePresent`

Latest focused validation: `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictAgentcModuleTest.*:EdictHelloLoopTest.*:EdictStatefulLoopTest.*:EdictAgentRootTest.*:EdictLlmModuleTest.*'` passed 21/21 on 2026-05-14.

## Current Limits
- Full Edict-owned trim/summarize policy is not implemented yet.
- The legacy `cpp-agent/main.cpp` outer loop still exists as transitional host surface under G078.
- Provider handles/runtime resources should not be shared across future worker VMs in the G091 intern-worker MVP.
