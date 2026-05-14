# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-05-13

## Current Focus
AgentC/J3 is an advanced research prototype/internal-alpha moving toward an Edict-resident agent loop: Edict should own provider/session/control-plane semantics while C++ remains the native transport, persistence, credential, and lifecycle substrate.

Latest completed slices: 🔗[G088 — Refresh README Current State](./Knowledge/Goals/G088-RefreshReadmeCurrentState/index.md) rewrote the README around Edict-resident agent control-plane ownership and intern-worker direction, 🔗[G087 — Prefer Adjacent Edict Eval Sigil](./Knowledge/Goals/G087-PreferAdjacentEvalSigil/index.md) updated Edict docs/scripts/modules/tests to prefer `word!` style, 🔗[G086 — Live Google LLM Regression Coverage](./Knowledge/Goals/G086-LiveGoogleLlmRegressionCoverage/index.md) added credential-gated live Google/Gemma gtest coverage, 🔗[G084 — Remove Dead Dictionary Payload Path](./Knowledge/Goals/G084-RemoveDeadDictionaryPayload/index.md) removed the obsolete compiler-side Dictionary bytecode payload, 🔗[G085 — Split Edict VM Translation Unit](./Knowledge/Goals/G085-SplitEdictVmTranslationUnit/index.md) split the VM implementation into core/FFI/bootstrap units, 🔗[G079 — Edict Agent Loop Tool Support](./Knowledge/Goals/G079-EdictAgentLoopToolSupport/index.md) added a narrow Edict/FFI file/shell tool surface, and 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) added the decoupled ghost-queue stream path. Immediate next implementation slice remains 🔗[G080 — LLM REPL Context Management](./Knowledge/Goals/G080-LlmReplContextManagement/index.md).

## Open Goals

### Active / Next
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — **ACTIVE** parent track; provider contracts, `llm.init(...)`, curated launcher, provider REPL, Codex provider, tool surface, streaming surface, and key VM semantics are landed. Remaining debt: C++ contract mirroring, host-owned outer UX seams, context management, and live notebook/FFI docs.
- 🔗[G080 — LLM REPL Context Management](./Knowledge/Goals/G080-LlmReplContextManagement/index.md) — **NEXT**; add explicit provider conversation reset/trim/summarize/inspect behavior.

### Recently Completed
- 🔗[G088 — Refresh README Current State](./Knowledge/Goals/G088-RefreshReadmeCurrentState/index.md) — **COMPLETE** (2026-05-13); rewrote README to match current AgentC architecture, launcher/provider surface, validation caveats, and intern-worker direction.
- 🔗[G087 — Prefer Adjacent Edict Eval Sigil](./Knowledge/Goals/G087-PreferAdjacentEvalSigil/index.md) — **COMPLETE** (2026-05-13); updated Edict-facing docs, modules, scripts, demos, and test snippets to prefer adjacent eval spelling and fixed stale-binary IPC prompt-flood validation.
- 🔗[G086 — Live Google LLM Regression Coverage](./Knowledge/Goals/G086-LiveGoogleLlmRegressionCoverage/index.md) — **COMPLETE** (2026-05-13); added credential-gated live Google/Gemma gtest coverage for `llm.init([gemma-4-31b-it])` through the real runtime.
- 🔗[G085 — Split Edict VM Translation Unit](./Knowledge/Goals/G085-SplitEdictVmTranslationUnit/index.md) — **COMPLETE** (2026-05-13); split `edict_vm.cpp` into core, FFI, and bootstrap implementation units while keeping dispatch centralized.
- 🔗[G084 — Remove Dead Dictionary Payload Path](./Knowledge/Goals/G084-RemoveDeadDictionaryPayload/index.md) — **COMPLETE** (2026-05-13); removed the obsolete `Dictionary` / `VALUE_DICTIONARY` / `VMEXT_DICT` path while preserving JSON object literal behavior.
- 🔗[G079 — Edict Agent Loop Tool Support](./Knowledge/Goals/G079-EdictAgentLoopToolSupport/index.md) — **COMPLETE** (2026-05-11); first practical file/shell action surface for the Edict-owned provider loop.
- 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) — **COMPLETE** (2026-05-11); detached provider workers push deltas to C++ queues and Edict synchronizes them on the VM/main thread.

### Planned
None.

### Deferred
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — **DEFERRED** research track; revisit after the Edict agent loop has stable tool and context behavior.

### Blocked
None.

## Active Context
- **Completed goals retired**: G068, G071, G072, G073, G076, G077, G081, G082, and G083 were moved to `LocalContext/Knowledge/Archive/Goals/`; see 🔗[Archive Index](./Knowledge/Archive/ARCHIVE_INDEX.md).
- **Edict provider surface**: `llm.init(name)` returns stable provider objects. Current request pattern is `provider < [prompt] request! > pop /`; launcher-backed REPL pattern is `provider < repl! > pop /`.
- **Curated launcher**: `./edict.sh` injects `EDICT_PATH`, preloads `agentc_curated.edict`, configures the `llm` bootstrap surface, and defaults to `EDICT_AUTO_CHAT=1` with `EDICT_DEFAULT_PRESET=local-qwen`. Set `EDICT_AUTO_CHAT=0` for raw curated Edict execution.
- **OpenAI Codex provider**: `openai-codex` is live through pi ChatGPT OAuth credentials in `~/.pi/agent/auth.json`; default model is `gpt-5.3-codex` with low reasoning effort. Live smoke test returned `ok`. Lower attempted Codex model names were unsupported for the account.
- **Important VM semantics now fixed/documented**: concatenated prefix sigils apply to the same identifier left-to-right (`/@name`, `//name`, etc.); leading `-name` selects the tail/oldest dictionary value for lookup, assignment, and removal; strict/lax unresolved lookup modes exist (`lax!`, `strict!`, `strict_null!`, `strict_fail!`).
- **Tool surface landed (G079)**: `extensions/agentc_stdlib` now exposes JSON-envelope file read/write/exact-replace and shell execution helpers. `agentc.edict` wraps them as `agentc_file_read!`, `agentc_file_write!`, `agentc_file_replace!`, `agentc_shell!`, and `agentc_tools`; `llm.edict` attaches `agentc_tools` as `provider.tools` for provider-context use.
- **Streaming surface landed (G074)**: `Runtime::stream_request_json(...)` now launches a detached provider worker that pushes text deltas into `StreamManager`. `agentc_runtime_stream_sync_json(...)` returns an `ok`/`complete` JSON envelope, `agentc.edict` wraps it as `agentc_call_stream!` / `agentc_stream_sync!`, and `llm.edict` provider objects expose `stream_start` / `stream_sync`. Live Google/Gemma smoke with `gemma-4-31b-it` returned `ok`.
- **Validation baseline from latest implementation pass**: `cmake --build build --target edict_tests -j2` passed; focused `edict_tests` style/regression slice passed 53/53 including `PiSimulationTest.MiniKanrenLogicExample`; `cmake --build build --target cpp_agent_tests -j2` passed; focused cpp-agent Edict/LLM suite passed 19/19 including the live Google/Gemma `gemma-4-31b-it` gtest returning `ok`; launcher smoke `EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.catalog! to_json! print'` passed.
- **Documentation direction**: README now reflects the current Edict-resident control-plane vision, current launcher/provider surface, and intern-worker direction. 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) and 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md) remain the key LLM-facing deep references. Next documentation improvement is a live notebook style with executable examples, especially for FFI/Cartographer.

## Handoff Note

**Current State**: The foundational persistence/client-host work has been retired to the archive. The live project is now centered on G078: making Edict the authoritative control plane for the agent loop. The codebase has a working curated launcher, Edict-side provider objects, provider REPL, local/Codex/Google-Gemma provider paths, first file/shell tool surface, first real-time ghost-queue stream surface, strict lookup modes, corrected prefix-chain semantics, and corrected tail-history semantics.

**Next Action**: Start G080. Add explicit context-management behavior to `provider.repl()` / provider-owned conversation state: likely reset/clear, inspect, trim, or summarize as the first narrow slice.

**Key Constraints**: Preserve stable provider-object in-place mutation; avoid expensive Codex `gpt-5.5`; keep Codex default reasoning low; use explicit scalar success/failure sentinels for `& |` control flow; keep Listree mutations on the VM/main thread for any future streaming architecture.

## Active Agents
None.

## Knowledge Inventory

### Goals — active tree (10)
- 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) — completed first stream surface; archive candidate on next cleanup.
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — deferred speculative reasoning architecture.
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — active parent track.
- 🔗[G079 — Edict Agent Loop Tool Support](./Knowledge/Goals/G079-EdictAgentLoopToolSupport/index.md) — completed first tool surface; archive candidate on next cleanup.
- 🔗[G080 — LLM REPL Context Management](./Knowledge/Goals/G080-LlmReplContextManagement/index.md) — immediate next slice.
- 🔗[G084 — Remove Dead Dictionary Payload Path](./Knowledge/Goals/G084-RemoveDeadDictionaryPayload/index.md) — completed VM/compiler cleanup; archive candidate on next cleanup.
- 🔗[G085 — Split Edict VM Translation Unit](./Knowledge/Goals/G085-SplitEdictVmTranslationUnit/index.md) — completed VM source-organization refactor; archive candidate on next cleanup.
- 🔗[G086 — Live Google LLM Regression Coverage](./Knowledge/Goals/G086-LiveGoogleLlmRegressionCoverage/index.md) — completed credential-gated live Google/Gemma gtest; archive candidate on next cleanup.
- 🔗[G087 — Prefer Adjacent Edict Eval Sigil](./Knowledge/Goals/G087-PreferAdjacentEvalSigil/index.md) — completed documentation/script/module style update to adjacent eval spelling; archive candidate on next cleanup.
- 🔗[G088 — Refresh README Current State](./Knowledge/Goals/G088-RefreshReadmeCurrentState/index.md) — completed README rewrite to current architecture/status; archive candidate on next cleanup.

### WorkProducts — active references (12)
- 🔗[AgentLang](./Knowledge/WorkProducts/AgentLang.md)
- 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md)
- 🔗[WP — C++ Agent Runtime File Structure](./Knowledge/WorkProducts/WP_CppAgentRuntimeFileStructure.md)
- 🔗[WP — Edict Native Agent Module Architecture](./Knowledge/WorkProducts/WP_EdictNativeAgentModuleArchitecture.md)
- 🔗[WP — Edict-Resident Agent Loop Consolidation Plan](./Knowledge/WorkProducts/WP-EdictResidentAgentLoopConsolidation-2026-05-10/index.md)
- 🔗[WP — Embedded Persistent Agent Architecture](./Knowledge/WorkProducts/WP_EmbeddedPersistentAgentArchitecture.md)
- 🔗[WP — G070 Ownership Boundary Map](./Knowledge/WorkProducts/WP_G070_OwnershipBoundaryMap_2026-05-01.md)
- 🔗[WP — G074 Real-time FFI Token Streaming](./Knowledge/WorkProducts/WP-G074-RealtimeFFITokenStreaming-2026-05-11/index.md)
- 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md)
- 🔗[WP — LMDB Surface Area Audit](./Knowledge/WorkProducts/WP_LMDB_SurfaceArea_Audit_2026-04-30.md)
- 🔗[WP — Native C++ Agent Core Audit](./Knowledge/WorkProducts/WP_NativeCppAgentCore_Audit.md)
- 🔗[WP — Session Image Persistence Plan](./Knowledge/WorkProducts/WP_SessionImagePersistencePlan_2026-05-01.md)

### Facts / Contracts / Procedures / Prompts
- 🔗[Concepts Index](./Knowledge/Concepts/index.md)
- 🔗[Facts Index](./Knowledge/Facts/index.md)
- 🔗[Listree Traversal Cycle Detection](./Knowledge/Facts/ListreeTraversalCycleDetection.md)
- 🔗[ListreeValue Serialization Format](./Knowledge/Facts/ListreeValueSerializationFormat.md)
- 🔗[AgentC Eval Contract](./Knowledge/Contracts/AgentcEvalContract.md)
- 🔗[AgentC Runtime C ABI](./Knowledge/Contracts/AgentcRuntimeCAbi.md)
- 🔗[AgentC Runtime JSON Contract](./Knowledge/Contracts/AgentcRuntimeJsonContract.md)
- 🔗[Procedures Index](./Knowledge/Procedures/index.md)
- 🔗[AgentC IPC Bridge Ops](./Knowledge/Procedures/AgentC_IPC_Bridge_Ops.md)
- 🔗[AgentC Socket Ops](./Knowledge/Procedures/AgentC_Socket_Ops.md)
- 🔗[System Prompt — AgentC](./Knowledge/Prompts/SystemPrompt_AgentC.md)

### Archive
- 🔗[Archive Index](./Knowledge/Archive/ARCHIVE_INDEX.md) — retired goals/work products preserved outside the active tree.

## Timeline
See 🔗[Timeline.md](./Timeline.md) for project history.

## Session Compliance
- [x] Reviewed Dashboard and HRM bootstrap instructions
- [x] Created/updated G084, G085, G086, G087, and G088 goal files
- [x] Implemented VM/compiler cleanup, VM translation-unit split, live Google/Gemma test coverage, adjacent-eval documentation/script style update, and README current-state rewrite
- [x] Ran focused build/test validation, including live LLM coverage and launcher smoke; README formatting/link checks passed
- [x] Updated Dashboard
- [x] Updated Timeline
