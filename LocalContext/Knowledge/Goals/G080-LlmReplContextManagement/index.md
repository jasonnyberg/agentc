# Goal: G080 — LLM REPL Context Management

**Status**: COMPLETE

**Created**: 2026-05-10  
**Reassessed**: 2026-05-11

## Objective
Add explicit context-management behavior to the launcher-backed `llm` provider REPL so long-running chat sessions can manage conversation state intentionally instead of only accumulating raw history.

## Rationale
`provider.repl()` is now live and usable, but it currently behaves like a thin multi-turn chat loop over monotonically growing conversation state. The next UX step is explicit context management inside that loop: trimming, summarizing, checkpointing, resetting, or otherwise controlling what the provider keeps and sends.

## Acceptance Criteria
- [x] Define a first explicit context-management contract for provider-owned conversation state.
- [x] Add at least one practical context-management operation to the REPL loop.
- [x] Ensure the context-management behavior is driven from Edict/provider state rather than host-owned policy.
- [x] Demonstrate the behavior through launcher-backed REPL validation.

## Candidate Directions
- Reset or clear conversation state
- Summarize older turns into compact retained state
- Limit retained history by policy
- Expose explicit provider/context inspection helpers

## Constraints
- Respect the current Edict truthiness/control-flow sharp edges documented in the VM guide.
- Keep provider objects stable and mutating in place.
- Build on the current launcher-backed `provider.repl()` seam instead of introducing a parallel host loop.

## Reassessment — 2026-05-11
This is now the immediate next slice after G079 landed the first tool-use surface and G074 landed the first real-time stream surface. Context management becomes more important once the loop can run longer engineering sessions with tools and streams. The first useful slice should likely be simple and inspectable: reset/clear history, summarize older turns, or enforce a retained-history limit through provider-owned state.

## Implementation — 2026-05-14
- Added a first provider-owned context contract in `cpp-agent/edict/modules/llm.edict`:
  - `provider.context_reset!` clears `provider.conversation`, resets assistant/stream state, and preserves the provider object plus system prompt.
  - `provider.context_inspect!` returns a JSON-serializable summary containing preset, provider/model, system prompt, messages, assistant text, last response/error, and stream status.
- Added launcher-backed REPL slash commands implemented in Edict/provider state:
  - `/reset` and `/clear` call `context_reset!`.
  - `/context` and `/inspect` call `context_inspect!` and print the JSON summary.
- Added a generic stdlib string equality helper (`agentc_ext_string_equals_ltv_value` wrapped as `agentc_string_equals!`) so command dispatch compares raw input strings without evaling arbitrary chat text as Edict.
- Added regression coverage:
  - `EdictLlmModuleTest.ProviderContextResetClearsConversationInPlace`
  - `EdictLlmModuleTest.ProviderReplSupportsContextSlashCommands`

## Validation — 2026-05-14
- `cmake --build build --target cpp_agent_tests -j2` — passed.
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictLlmModuleTest.*'` — passed 11/11 including live Google/Gemma coverage.
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictAgentcModuleTest.*:EdictHelloLoopTest.*:EdictStatefulLoopTest.*:EdictAgentRootTest.*:EdictLlmModuleTest.*'` — passed 21/21.

## Relationship To G078
G078 establishes the Edict-owned loop. G080 makes that loop sustainable for longer-running interactions by adding first-class context management.
