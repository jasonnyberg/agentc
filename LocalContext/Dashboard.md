# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-05-11

## Current Focus
AgentC/J3 is an advanced research prototype/internal-alpha moving toward an Edict-resident agent loop: Edict should own provider/session/control-plane semantics while C++ remains the native transport, persistence, credential, and lifecycle substrate.

Latest completed slices: 🔗[G079 — Edict Agent Loop Tool Support](./Knowledge/Goals/G079-EdictAgentLoopToolSupport/index.md) added a narrow Edict/FFI file/shell tool surface, and 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) added the decoupled ghost-queue stream path. Immediate next implementation slice: 🔗[G080 — LLM REPL Context Management](./Knowledge/Goals/G080-LlmReplContextManagement/index.md).

## Open Goals

### Active / Next
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — **ACTIVE** parent track; provider contracts, `llm.init(...)`, curated launcher, provider REPL, Codex provider, tool surface, streaming surface, and key VM semantics are landed. Remaining debt: C++ contract mirroring, host-owned outer UX seams, context management, and live notebook/FFI docs.
- 🔗[G080 — LLM REPL Context Management](./Knowledge/Goals/G080-LlmReplContextManagement/index.md) — **NEXT**; add explicit provider conversation reset/trim/summarize/inspect behavior.

### Recently Completed
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
- **Edict provider surface**: `llm.init(name)` returns stable provider objects. Current request pattern is `provider < [prompt] request ! > pop /`; launcher-backed REPL pattern is `provider < repl ! > pop /`.
- **Curated launcher**: `./edict.sh` injects `EDICT_PATH`, preloads `agentc_curated.edict`, configures the `llm` bootstrap surface, and defaults to `EDICT_AUTO_CHAT=1` with `EDICT_DEFAULT_PRESET=local-qwen`. Set `EDICT_AUTO_CHAT=0` for raw curated Edict execution.
- **OpenAI Codex provider**: `openai-codex` is live through pi ChatGPT OAuth credentials in `~/.pi/agent/auth.json`; default model is `gpt-5.3-codex` with low reasoning effort. Live smoke test returned `ok`. Lower attempted Codex model names were unsupported for the account.
- **Important VM semantics now fixed/documented**: concatenated prefix sigils apply to the same identifier left-to-right (`/@name`, `//name`, etc.); leading `-name` selects the tail/oldest dictionary value for lookup, assignment, and removal; strict/lax unresolved lookup modes exist (`lax!`, `strict!`, `strict_null!`, `strict_fail!`).
- **Tool surface landed (G079)**: `extensions/agentc_stdlib` now exposes JSON-envelope file read/write/exact-replace and shell execution helpers. `agentc.edict` wraps them as `agentc_file_read !`, `agentc_file_write !`, `agentc_file_replace !`, `agentc_shell !`, and `agentc_tools`; `llm.edict` attaches `agentc_tools` as `provider.tools` for provider-context use.
- **Streaming surface landed (G074)**: `Runtime::stream_request_json(...)` now launches a detached provider worker that pushes text deltas into `StreamManager`. `agentc_runtime_stream_sync_json(...)` returns an `ok`/`complete` JSON envelope, `agentc.edict` wraps it as `agentc_call_stream !` / `agentc_stream_sync !`, and `llm.edict` provider objects expose `stream_start` / `stream_sync`. Live Google/Gemma smoke with `gemma-4-31b-it` returned `ok`.
- **Validation baseline from latest implementation pass**: `./build/edict/edict_tests --gtest_filter='EdictVM.*'` passed 22/22; focused `cpp_agent_tests` for stream/tool/LLM/stateful/root seams passed 28/28.
- **Documentation direction**: 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) and 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md) are the key LLM-facing references. Next documentation improvement is a live notebook style with executable examples, especially for FFI/Cartographer.

## Handoff Note

**Current State**: The foundational persistence/client-host work has been retired to the archive. The live project is now centered on G078: making Edict the authoritative control plane for the agent loop. The codebase has a working curated launcher, Edict-side provider objects, provider REPL, local/Codex/Google-Gemma provider paths, first file/shell tool surface, first real-time ghost-queue stream surface, strict lookup modes, corrected prefix-chain semantics, and corrected tail-history semantics.

**Next Action**: Start G080. Add explicit context-management behavior to `provider.repl()` / provider-owned conversation state: likely reset/clear, inspect, trim, or summarize as the first narrow slice.

**Key Constraints**: Preserve stable provider-object in-place mutation; avoid expensive Codex `gpt-5.5`; keep Codex default reasoning low; use explicit scalar success/failure sentinels for `& |` control flow; keep Listree mutations on the VM/main thread for any future streaming architecture.

## Active Agents
None.

## Knowledge Inventory

### Goals — active tree (5)
- 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) — completed first stream surface; archive candidate on next cleanup.
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — deferred speculative reasoning architecture.
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — active parent track.
- 🔗[G079 — Edict Agent Loop Tool Support](./Knowledge/Goals/G079-EdictAgentLoopToolSupport/index.md) — completed first tool surface; archive candidate on next cleanup.
- 🔗[G080 — LLM REPL Context Management](./Knowledge/Goals/G080-LlmReplContextManagement/index.md) — immediate next slice.

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
- [x] Reassessed incomplete goals
- [x] Retired completed goals to Archive
- [x] Updated Archive Index
- [x] Rebuilt Dashboard around active/open work
- [x] Updated Timeline
