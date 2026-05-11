# Goal: G081 — OpenAI Codex Subscription Provider

**Status**: COMPLETE  
**Created**: 2026-05-10  
**Completed**: 2026-05-10

## Objective
Add a first AgentC runtime/provider path for OpenAI Codex subscription models using pi's existing ChatGPT Plus/Pro OAuth credentials.

## Rationale
Pi already supports `openai-codex` as a subscription-backed provider using the ChatGPT backend Codex Responses endpoint. AgentC can reuse the same persisted OAuth credentials from `~/.pi/agent/auth.json` to test the Edict-owned `llm.init(...)` provider seam against a live subscription-backed OpenAI coding model without requiring OpenAI API-key billing.

## Acceptance Criteria
- [x] Read OpenAI Codex OAuth credentials from pi auth state and refresh them when expired.
- [x] Register a native `openai-codex-responses` runtime provider.
- [x] Send a minimal SSE Codex Responses request to `https://chatgpt.com/backend-api/codex/responses`.
- [x] Add Edict provider contract and `llm.init([openai-codex])` preset support.
- [x] Use a lower-tier working model for validation instead of `gpt-5.5`.
- [x] Add durable regression coverage for the Edict preset surface.
- [x] Validate the path with a live low-cost request.

## Implementation Notes
- Added `cpp-agent/runtime/providers/openai_codex/openai_codex_provider.*` and registered it as `openai-codex-responses`.
- Added `OpenAICodexCredentials` plus `get_openai_codex_credentials()` in `cpp-agent/runtime/common/credentials.*`, reusing pi's `~/.pi/agent/auth.json` `openai-codex` OAuth entry.
- The provider uses ChatGPT Codex SSE only for this first slice; WebSocket continuation/cached transport from pi is intentionally not implemented yet.
- The first attempted low-tier model, `gpt-5.1-codex-mini`, returned `400` / unsupported for the current ChatGPT account. `gpt-5.3-codex` succeeded and is the current default lower-than-`gpt-5.5` preset.
- Codex request default reasoning effort is `low` to minimize live validation cost. `StreamOptions.reasoning_effort` is wired through runtime request options for future overrides.

## Validation
- `cmake --build build --target agent_runtime cpp_agent_tests -j2`
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictLlmModuleTest.*:EdictAgentRootTest.*:AgentRootVmOpsTest.*:SessionStateStoreTest.PersistsDeclarativeRuntimeRehydrationMetadataWithoutTransientHandles'`
- Live smoke test:
  - `EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.init([openai-codex]) @provider provider < [Reply with exactly two lowercase letters: ok] request ! > pop / provider.assistant_text print'`
  - Result: `ok` via `gpt-5.3-codex` with `reasoning.effort = low`.

## Follow-up Opportunities
- Add native AgentC OAuth login instead of relying on pi's auth file.
- Add WebSocket/cached-context transport parity with pi.
- Add tool-call parsing for full Codex agent-loop support once G079 tool surfaces land.
- Add explicit Edict/provider controls for reasoning effort and model selection.
