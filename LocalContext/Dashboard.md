# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-05-14

## Current Focus
AgentC/J3 is an advanced research prototype/internal-alpha moving toward an Edict-resident agent loop: Edict should own provider/session/control-plane semantics while C++ remains the native transport, persistence, credential, and lifecycle substrate.

Completed implementation/documentation slices are retired from the open-dashboard view; the incomplete backlog is listed below in priority order. Work has begun on 🔗[G091 — Intern Worker Concurrency MVP](./Knowledge/Goals/G091-InternWorkerConcurrencyMvp/index.md) under the active parent track 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md): the first deterministic `intern_run!` substrate slice is implemented and under validation/hardening.

## Incomplete Goals — Priority Order

1. 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — **ACTIVE** parent track; make Edict the authoritative provider/session/tool/context control plane.
2. 🔗[G091 — Intern Worker Concurrency MVP](./Knowledge/Goals/G091-InternWorkerConcurrencyMvp/index.md) — **ACTIVE**; first deterministic `intern_run!` worker-VM dispatch slice is implemented; remaining hardening is explicit worker arena/slab lifecycle and/or scheduler scope before closure.
3. 🔗[G099 — Intern Task Quality Contracts](./Knowledge/Goals/G099-InternTaskQualityContracts/index.md) — **PLANNED**; bounded/checkable task schemas for local intern agents.
4. 🔗[G092 — Cartographer FFI Re-entrancy Metadata](./Knowledge/Goals/G092-CartographerFfiReentrancyMetadata/index.md) — **PLANNED**; classify imported symbols so worker VMs share only safe native capabilities.
5. 🔗[G096 — Authoritative mmap Session Resume](./Knowledge/Goals/G096-AuthoritativeMmapSessionResume/index.md) — **PLANNED**; harden full durable mmap-backed cognitive-state resume beyond current normal-exit sessions.
6. 🔗[G101 — Direct Edict Tool-Emission Path](./Knowledge/Goals/G101-EdictDirectToolEmissionPath/index.md) — **PLANNED**; guarded compact model-emitted Edict as an alternative to JSON tool-call glue.
7. 🔗[G100 — Edict Isolation Contract Hardening](./Knowledge/Goals/G100-EdictIsolationContractHardening/index.md) — **PLANNED**; test/document isolated-call safety for LLM-generated Edict.
8. 🔗[G094 — Curated Native Cognitive Capability Libraries](./Knowledge/Goals/G094-CuratedNativeCognitiveLibraries/index.md) — **PLANNED**; structural diff, AST/tree-sitter, and persistent knowledge-graph capability surfaces.
9. 🔗[G095 — Edict Cognitive Skill Scaffolds](./Knowledge/Goals/G095-EdictCognitiveSkillScaffolds/index.md) — **PLANNED**; reusable investigation, code-review, and refactoring-planner scaffolds.
10. 🔗[G097 — Composite Speculation + Logic + FFI Demo](./Knowledge/Goals/G097-CompositeSpeculationLogicFfiDemo/index.md) — **PLANNED**; showcase speculation + miniKanren + FFI + persistence composition.
11. 🔗[G098 — Architectural Vision Work Product](./Knowledge/Goals/G098-ArchitecturalVisionWorkProduct/index.md) — **PLANNED**; preserve architectural/intern-concurrency vision as a durable WorkProduct.
12. 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — **DEFERRED**; revisit ToT/MCTS/ReAct-style native speculation after loop/tool/context stability.
13. 🔗[G093 — Reference-Scoped ReadOnly Sharing](./Knowledge/Goals/G093-ReferenceScopedReadOnlySharing/index.md) — **DEFERRED**; finer-grained ReadOnly/shadowing after whole-subtree sharing proves insufficient.

### Blocked
None.

## Active Context
- **Completed goals retired from open view**: G074, G079, G080, G084, G085, G086, G087, G088, G089, G090, and G102 are complete and no longer listed in the incomplete-priority backlog. Older completed goals G068, G071, G072, G073, G076, G077, G081, G082, and G083 were moved to `LocalContext/Knowledge/Archive/Goals/`; see 🔗[Archive Index](./Knowledge/Archive/ARCHIVE_INDEX.md). Completed-but-unarchived goals remain archive candidates for the next archival cleanup.
- **Edict provider surface**: `llm.init(name)` returns stable provider objects. Current request pattern is `provider < [prompt] request! > / /`; launcher-backed REPL pattern is `provider < repl! > / /`. G080 added provider-owned `context_reset!` / `context_inspect!` plus REPL slash commands `/reset`, `/clear`, `/context`, and `/inspect`.
- **Intern worker surface (G091 first slice)**: raw Edict now has `intern_run!`, which accepts a bounded task envelope, freezes shared `context`/`imports`, snapshots `input`, runs deterministic Edict source in a fresh worker VM with private `workspace`, joins, and returns a structured result copied back on the coordinator thread. See 🔗[K032 — Edict Intern Worker Surface](./Knowledge/Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md).
- **Curated launcher**: `./edict.sh` injects `EDICT_PATH`, preloads `agentc_curated.edict`, configures the `llm` bootstrap surface, and defaults to `EDICT_AUTO_CHAT=1` with `EDICT_DEFAULT_PRESET=local-qwen`. Set `EDICT_AUTO_CHAT=0` for raw curated Edict execution.
- **OpenAI Codex provider**: `openai-codex` is live through pi ChatGPT OAuth credentials in `~/.pi/agent/auth.json`; default model is `gpt-5.3-codex` with low reasoning effort. Live smoke test returned `ok`. Lower attempted Codex model names were unsupported for the account.
- **Important VM semantics now fixed/documented**: concatenated prefix sigils apply to the same identifier left-to-right (`/@name`, `//name`, etc.); leading `-name` selects the tail/oldest dictionary value for lookup, assignment, and removal; strict/lax unresolved lookup modes exist (`lax!`, `strict!`, `strict_null!`, `strict_fail!`); bare `/` is the documented stack discard and `pop` is no longer a compiler/bootstrap alias.
- **Raw Edict named sessions (G102)**: `./build/edict/edict --session ID` creates/resumes a root scope under `/tmp/session/<id>/` by default; `--session-base DIR` or `EDICT_SESSION_BASE` changes the base. Session ids are filesystem-safe (`[A-Za-z0-9._-]+`, excluding `.`/`..`). This uses the current session-image/slab store on normal process exit; full kill-mid-turn authoritative mmap resume remains G096.
- **Tool surface landed (G079)**: `extensions/agentc_stdlib` now exposes JSON-envelope file read/write/exact-replace and shell execution helpers. `agentc.edict` wraps them as `agentc_file_read!`, `agentc_file_write!`, `agentc_file_replace!`, `agentc_shell!`, and `agentc_tools`; `llm.edict` attaches `agentc_tools` as `provider.tools` for provider-context use.
- **Streaming surface landed (G074)**: `Runtime::stream_request_json(...)` now launches a detached provider worker that pushes text deltas into `StreamManager`. `agentc_runtime_stream_sync_json(...)` returns an `ok`/`complete` JSON envelope, `agentc.edict` wraps it as `agentc_call_stream!` / `agentc_stream_sync!`, and `llm.edict` provider objects expose `stream_start` / `stream_sync`. Live Google/Gemma smoke with `gemma-4-31b-it` returned `ok`.
- **Validation baseline from latest implementation pass**: `cmake --build build --target edict_tests -j2` passed; `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*'` passed 2/2 for the G091 first slice; focused pop-transition tests passed 19/19; focused `edict_tests` style/regression slice passed 59/59 including `PiSimulationTest.MiniKanrenLogicExample`; `cmake --build build --target cpp_agent_tests -j2` passed; focused cpp-agent Edict/LLM suite now passes 21/21 including the live Google/Gemma `gemma-4-31b-it` gtest and the new G080 REPL context-management coverage.
- **Documentation direction**: README now targets potential users with AgentC's unique value, applications, runnable patterns, maturity expectations, and roadmap. 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) and 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md) remain the key LLM-facing deep references. Next documentation improvement is a live notebook style with executable examples, especially for FFI/Cartographer.
- **Incomplete backlog priority**: current order is G078, G091, G099, G092, G096, G101, G100, G094, G095, G097, G098, G075, G093. G093 remains deliberately deferred until the simpler whole-subtree ReadOnly worker MVP is proven.

## Handoff Note

**Current State**: The foundational persistence/client-host work has been retired to the archive. The live project is now centered on G078: making Edict the authoritative control plane for the agent loop. The codebase has a working curated launcher, Edict-side provider objects, provider REPL, local/Codex/Google-Gemma provider paths, provider-owned reset/inspect context management, deterministic `intern_run!` worker dispatch, first file/shell tool surface, first real-time ghost-queue stream surface, strict lookup modes, corrected prefix-chain semantics, and corrected tail-history semantics.

**Next Action**: Continue hardening G091 after the landed deterministic `intern_run!` slice: decide whether to close the MVP now or add explicit worker arena/slab lifecycle cleanup and/or a minimal multi-worker scheduler before promoting G099 task-quality contracts.

**Key Constraints**: Preserve stable provider-object in-place mutation; avoid expensive Codex `gpt-5.5`; keep Codex default reasoning low; use explicit scalar success/failure sentinels for `& |` control flow; keep Listree mutations on the VM/main thread for any future streaming architecture.

## Active Agents
None.

## Knowledge Inventory

### Goals — incomplete priority view (13)
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — active parent track.
- 🔗[G091 — Intern Worker Concurrency MVP](./Knowledge/Goals/G091-InternWorkerConcurrencyMvp/index.md) — active local-intern worker architecture slice; first `intern_run!` substrate landed.
- 🔗[G099 — Intern Task Quality Contracts](./Knowledge/Goals/G099-InternTaskQualityContracts/index.md) — planned task-contract layer for bounded/checkable intern work.
- 🔗[G092 — Cartographer FFI Re-entrancy Metadata](./Knowledge/Goals/G092-CartographerFfiReentrancyMetadata/index.md) — planned FFI safety metadata for worker sharing.
- 🔗[G096 — Authoritative mmap Session Resume](./Knowledge/Goals/G096-AuthoritativeMmapSessionResume/index.md) — planned durable mmap-backed cognitive-state resume hardening.
- 🔗[G101 — Direct Edict Tool-Emission Path](./Knowledge/Goals/G101-EdictDirectToolEmissionPath/index.md) — planned guarded direct Edict tool-use path.
- 🔗[G100 — Edict Isolation Contract Hardening](./Knowledge/Goals/G100-EdictIsolationContractHardening/index.md) — planned test/doc hardening for isolated LLM-emitted Edict.
- 🔗[G094 — Curated Native Cognitive Capability Libraries](./Knowledge/Goals/G094-CuratedNativeCognitiveLibraries/index.md) — planned structural diff, AST/tree-sitter, and knowledge-graph capabilities.
- 🔗[G095 — Edict Cognitive Skill Scaffolds](./Knowledge/Goals/G095-EdictCognitiveSkillScaffolds/index.md) — planned investigation/review/refactor scaffolds.
- 🔗[G097 — Composite Speculation + Logic + FFI Demo](./Knowledge/Goals/G097-CompositeSpeculationLogicFfiDemo/index.md) — planned integrated showcase/demo.
- 🔗[G098 — Architectural Vision Work Product](./Knowledge/Goals/G098-ArchitecturalVisionWorkProduct/index.md) — planned durable work product for the extracted architecture summary.
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — deferred speculative reasoning architecture.
- 🔗[G093 — Reference-Scoped ReadOnly Sharing](./Knowledge/Goals/G093-ReferenceScopedReadOnlySharing/index.md) — deferred finer-grained ReadOnly/shadowing refinement.

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
- 🔗[Edict Provider Object Surface](./Knowledge/Facts/J3_AgentC/K031_Edict_Provider_Object_Surface.md)
- 🔗[Edict Intern Worker Surface](./Knowledge/Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md)
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
- [x] Created/updated G084–G102 goal files
- [x] Implemented VM/compiler cleanup, VM translation-unit split, live Google/Gemma test coverage, adjacent-eval documentation/script style update, README rewrites, `pop` keyword removal, architectural/intern-concurrency goal extraction, and raw Edict named-session startup support
- [x] Ran focused build/test validation, including live LLM coverage and launcher smoke; README example/link/style, pop-removal inventory, session CLI, and session-state regression checks passed
- [x] Retired completed goals from open-dashboard view and reordered incomplete backlog by priority
- [x] Completed G080 provider-owned LLM REPL context reset/inspect slice and began G091 with the deterministic `intern_run!` worker dispatch slice
- [x] Updated Dashboard
- [x] Updated Timeline
