# Dashboard

**Project**: AgentC / J3
**Last Updated**: 2026-06-21

## Current Focus
AgentC/J3 is an internal-alpha persistent cognition runtime. The Edict-resident control-plane track is complete: Edict owns provider/session/tool/context semantics through the curated launcher, provider objects, root/request construction, direct-action wrappers, intern-worker public words, cognitive capability libraries, cognitive skill scaffolds, and overlay dictionaries for reference-scoped ReadOnly sharing. C++ remains the native transport, persistence, credential, provider-adapter, Root1 broker, and lifecycle substrate. All planned priority goals (G074–G111 except G075) are complete and archived. The only remaining long-arc goal is G075 (speculative Edict native architectures), deferred until a concrete Tree-of-Thoughts/MCTS demonstration is needed.

## Incomplete Goals — Priority Order

1. 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — **DEFERRED**; revisit ToT/MCTS/ReAct-style native speculation after loop/tool/context stability. This is the last remaining goal in the active tree.

### Blocked
No active blockers. Full `edict_tests` green (198/198). `treesitter_tests` 28/28 (15 bridge + 6 diff + 7 KG).

## Active Context
- **Completed goals retired and archived**: G074, G078, G079, G080, G084, G085, G086, G087, G088, G089, G090, G091, G092, G093, G094, G095, G096, G097, G098, G099, G100, G101, G102, G103, G104, G105, G106, G107, G108, G109, G110, and G111 are complete and archived in `LocalContext/Knowledge/Archive/Goals/`; see 🔗[Archive Index](./Knowledge/Archive/ARCHIVE_INDEX.md). Older completed goals G068, G071, G072, G073, G076, G077, G081, G082, and G083 were archived in prior passes.
- **Edict provider surface**: `llm.init(name)` returns stable provider objects. Current request pattern is `provider < [prompt] request! > / /`; launcher-backed REPL pattern is `provider < repl! > / /`. G080 added provider-owned `context_reset!` / `context_inspect!` plus REPL slash commands `/reset`, `/clear`, `/context`, and `/inspect`.
- **Intern worker / micro-VM substrate**: module-backed Edict has `intern_run!`, `intern_start!`, `intern_sync!`, and `intern_cancel!` via imported worker primitives and `worker.edict` / `intern.edict`. Workers can run as thread workers, forked processes, or fork/exec processes with independently mounted static declaration images (G107). Quality contracts enforce bounded task schemas and result trust validators (G099). Overlay dictionaries provide reference-scoped ReadOnly sharing for worker-local shadow values (G093). The Root1 eventfd/epoll broker provides mailbox descriptors, resource keys/grants, `await!`, and scheduler persistence (G110).
- **Curated launcher**: `./edict.sh` injects `EDICT_PATH`, imports global `ext`/`runtimeffi` bindings for curated `agentc.edict` wrappers, preloads `agentc_curated.edict`, configures the `llm` bootstrap surface, and defaults to `EDICT_AUTO_CHAT=1` with `EDICT_DEFAULT_PRESET=local-qwen`. Set `EDICT_AUTO_CHAT=0` for raw curated Edict execution.
- **OpenAI Codex provider**: `openai-codex` is live through pi ChatGPT OAuth credentials in `~/.pi/agent/auth.json`; default model is `gpt-5.3-codex` with low reasoning effort. Live smoke test returned `ok`.
- **Important VM semantics now fixed/documented**: concatenated prefix sigils apply to the same identifier left-to-right (`/@name`, `//name`, etc.); leading `-name` selects the tail/oldest dictionary value for lookup, assignment, and removal; strict/lax unresolved lookup modes exist (`lax!`, `strict!`, `strict_null!`, `strict_fail!`); bare `/` is the documented stack discard and `pop` is no longer a compiler/bootstrap alias.
- **Raw Edict named sessions (G102)**: `./build/edict/edict --session ID` creates/resumes a root scope under `/tmp/session/<id>/` by default; `--session-base DIR` or `EDICT_SESSION_BASE` changes the base. Session ids are filesystem-safe (`[A-Za-z0-9._-]+`, excluding `.`/`..`). G096 covers deterministic root/scheduler/static-mount resume. Full kill-mid-op activation-frame resurrection remains future work.
- **Tool surface landed (G079/G101)**: `extensions/agentc_stdlib` exposes JSON-envelope file read/write/exact-replace and shell execution helpers. `agentc.edict` wraps them as `agentc_file_read!`, `agentc_file_write!`, `agentc_file_replace!`, `agentc_shell!`, and `agentc_tools`; `llm.edict` attaches `agentc_tools` as `provider.tools` for provider-context use. G101 adds `agentc_direct_action!` with an MVP `read_file` whitelist and denied-operation envelopes.
- **Streaming surface landed (G074)**: `Runtime::stream_request_json(...)` launches a detached provider worker that pushes text deltas into `StreamManager`. `agentc.edict` wraps it as `agentc_call_stream!` / `agentc_stream_sync!`.
- **Cognitive capability libraries (G094)**: Tree-sitter bridge (`treesitter.load!`/`parse!`/`list!`/`diff!`) and persistent knowledge graph (`kgraph.create!`/`add_node!`/`add_edge!`/`get_node!`/`query!`/`nodes!`/`edges!`) are ordinary Edict builtins.
- **Cognitive skill scaffolds (G095)**: `cognitive.edict` module provides investigation, code-review, and refactor-plan state machines with JSON-serializable state.
- **Overlay dictionaries (G093)**: `overlay.new!`/`set!`/`get!`/`has!`/`keys!`/`shadow_keys!`/`commit!` as 7 VM opcodes. Workers shadow keys on a mutable local dict while the frozen shared base stays ReadOnly.
- **Architectural vision (G098)**: `WP-AgentCArchitecturalVisionInternConcurrency-2026-06-21.md` preserves the full architectural vision, intern concurrency model, application patterns, implementation boundary, and roadmap.
- **Validation baseline from latest implementation pass**: G093 closeout passed focused OverlayDictionary 8/8 + demo PASS. Full `edict_tests` 198/198, `reflect_tests` 55/55, `listree_tests` 83/83, `cartographer_tests` 52/52, `cpp_agent_tests` 56/56, `treesitter_tests` 28/28.
- **Documentation direction**: README targets potential users with AgentC's unique value, applications, runnable patterns, maturity expectations, and roadmap. 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) and 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md) remain the key LLM-facing deep references. 🔗[WP — AgentC Architectural Vision & Intern Concurrency Model](./Knowledge/WorkProducts/WP-AgentCArchitecturalVisionInternConcurrency-2026-06-21.md) is the authoritative roadmap reference.
- **Incomplete backlog priority**: current order is G075. G078, G091, G093, G094, G095, G097, G098, G099, and G110 are COMPLETE and archived. The Root1 eventfd/epoll broker, Edict-resident parent control-plane substrate, intern-worker MVP, quality-contract MVP, cognitive scaffolds, overlay dictionaries, process-isolated workers, and architectural vision work product are all closed for their current scopes.

## Handoff Note

**Project**: AgentC/J3 is an internal-alpha persistent cognition runtime. Edict is the authoritative agent control plane over a C++ persistence/transport/concurrency substrate.

**Current State**: All planned priority goals are complete and archived. The completed track includes:
- G078: Edict-resident agent loop consolidation (provider/session/tool/context semantics)
- G091: Intern worker concurrency MVP (blocking/async dispatch, lifecycle/drop/abandon, cancellation)
- G093: Reference-scoped ReadOnly sharing via explicit overlay dictionaries (7 VM opcodes)
- G094: Curated native cognitive libraries (tree-sitter, structural diff, knowledge graph)
- G095: Edict cognitive skill scaffolds (investigation/code-review/refactor-plan)
- G096: Authoritative mmap session resume (deterministic root/scheduler/static-mount restore)
- G097: Composite speculation + logic + FFI demo
- G098: Architectural vision work product
- G099: Intern task quality contracts
- G100–G111: Isolation hardening, direct tool emission, session IDs, static declaration images, immutable code objects, ReadOnly slab ownership, Root1 publication registry, process-isolated workers, traversal bitmaps, ReadOnly mutation hardening, Root1 broker, worker primitive FFI migration

**Next Action**: The only remaining goal is G075 (speculative Edict native architectures), deferred until a concrete ToT/MCTS/ReAct demonstration is needed to validate Listree snapshot economics. Full baseline validation is green (edict_tests 198/198).

**Key Context**:
- **G093 is complete.** Explicit overlay dictionaries provide reference-scoped ReadOnly sharing: `overlay.new!`/`set!`/`get!`/`has!`/`keys!`/`shadow_keys!`/`commit!` as 7 new VM opcodes. Workers shadow keys on a mutable local dict while the frozen shared base stays ReadOnly. 📄[WP — G093](./Knowledge/WorkProducts/WP_G093_ReferenceScopedReadOnlySharing.md). Validation: OverlayDictionaryTest 8/8, `demo_overlay_dictionary` PASS.
- **G098 is complete.** `WP-AgentCArchitecturalVisionInternConcurrency-2026-06-21.md` preserves the full architectural vision, intern concurrency model, application patterns, implementation boundary, and roadmap.
- **G095 is complete.** `cognitive.edict` module provides investigation/code-review/refactor-plan scaffold constructors with stable Listree/JSON shapes.
- **G097 is complete.** `demo_composite_speculation_logic_ffi` composes Cartographer-imported FFI + miniKanren + durable Listree state.
- **G091 is complete.** Running `worker.edict_drop!` means handle abandonment, not thread preemption. Process-isolated workers (G107) support thread, fork, and fork/exec backends.
- **G099 is complete.** Validators remain opt-in for blocking `intern_run!`; async `intern_start!` enforces the minimal task contract before dispatch.
- **G107 is complete.** Process-isolated micro-VM interns support thread, fork, and fork/exec workers with independently mounted G103 static declaration images. Handle/capability policy: Inherited (frozen RO Listree), Rehydrated (JSON pipe/mmap), Blocked (fds/handles/credentials).
- **G110 is complete.** Root1 has eventfd/epoll broker, mailbox descriptors, resource keys/grants, `await!`, scheduler save/load. Full activation-frame resurrection remains future work.

**Do NOT**: Do not add new intern-specific VM opcodes, do not expose raw fds as durable Edict state, and do not pass stateful provider/runtime handles into intern worker context/imports. Full kill-mid-op activation-frame serialization and stable cross-version code-object identity remain future work.

**G075 activation criteria**: Start G075 when a concrete Tree-of-Thoughts, MCTS, or recursive ReAct demonstration is needed to validate slab snapshot/rollback economics for multi-branch reasoning. The prerequisite control-plane stability (G078/G079/G080) and worker isolation (G091/G093/G107/G110) are now in place.

## Active Agents
None.

## Knowledge Inventory

### Goals — incomplete priority view (1)
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — deferred speculative reasoning architecture (ToT/MCTS/ReAct).

### WorkProducts — active references (22)
- 🔗[AgentLang](./Knowledge/WorkProducts/AgentLang.md)
- 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md)
- 🔗[WP — C++ Agent Runtime File Structure](./Knowledge/WorkProducts/WP_CppAgentRuntimeFileStructure.md)
- 🔗[WP — Code Object / Activation State Boundary](./Knowledge/WorkProducts/WP-CodeObjectActivationBoundary-2026-05-25/index.md)
- 🔗[WP — Edict Native Agent Module Architecture](./Knowledge/WorkProducts/WP_EdictNativeAgentModuleArchitecture.md)
- 🔗[WP — Edict-Resident Agent Loop Consolidation Plan](./Knowledge/WorkProducts/WP-EdictResidentAgentLoopConsolidation-2026-05-10/index.md)
- 🔗[WP — Embedded Persistent Agent Architecture](./Knowledge/WorkProducts/WP_EmbeddedPersistentAgentArchitecture.md)
- 🔗[WP — G070 Ownership Boundary Map](./Knowledge/WorkProducts/WP_G070_OwnershipBoundaryMap_2026-05-01.md)
- 🔗[WP — G074 Real-time FFI Token Streaming](./Knowledge/WorkProducts/WP-G074-RealtimeFFITokenStreaming-2026-05-11/index.md)
- 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md)
- 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](./Knowledge/WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md)
- 🔗[WP — LMDB Surface Area Audit](./Knowledge/WorkProducts/WP_LMDB_SurfaceArea_Audit_2026-04-30.md)
- 🔗[WP — Native C++ Agent Core Audit](./Knowledge/WorkProducts/WP_NativeCppAgentCore_Audit.md)
- 🔗[WP — Session Image Persistence Plan](./Knowledge/WorkProducts/WP_SessionImagePersistencePlan_2026-05-01.md)
- 🔗[WP — G107 Process-Isolated Launch Contract](./Knowledge/WorkProducts/WP_G107_ProcessIsolatedLaunchContract.md)
- 🔗[WP — G096 Authoritative mmap Session Resume Boundary](./Knowledge/WorkProducts/WP_G096_AuthoritativeMmapSessionResume.md)
- 🔗[WP — G106 Root1 Slab Advertisement Registry](./Knowledge/WorkProducts/WP_G106_Root1SlabAdvertisementRegistry.md)
- 🔗[WP — G101 Direct Edict Tool-Emission Path](./Knowledge/WorkProducts/WP_G101_DirectEdictToolEmissionPath.md)
- 🔗[WP — G100 Edict Isolation Contract Hardening](./Knowledge/WorkProducts/WP_G100_EdictIsolationContractHardening.md)
- 🔗[WP — G095 Edict Cognitive Skill Scaffolds](./Knowledge/WorkProducts/WP_G095_EdictCognitiveSkillScaffolds.md)
- 🔗[WP — G097 Composite Speculation + Logic + FFI Demo](./Knowledge/WorkProducts/WP_G097_CompositeSpeculationLogicFfiDemo.md)
- 🔗[WP — AgentC Architectural Vision & Intern Concurrency Model](./Knowledge/WorkProducts/WP-AgentCArchitecturalVisionInternConcurrency-2026-06-21.md)
- 🔗[WP — G093 Reference-Scoped ReadOnly Sharing](./Knowledge/WorkProducts/WP_G093_ReferenceScopedReadOnlySharing.md)

### Concepts / Facts / Contracts / Procedures / Prompts
- 🔗[Concepts Index](./Knowledge/Concepts/index.md)
- 🔗[Layered mmap Micro-VM Architecture](./Knowledge/Concepts/LayeredMmapMicroVmArchitecture/index.md)
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
- [x] Created/updated G084–G111 goal files
- [x] Implemented VM/compiler cleanup, VM translation-unit split, live Google/Gemma test coverage, adjacent-eval documentation/script style update, README rewrites, `pop` keyword removal, architectural/intern-concurrency goal extraction, and raw Edict named-session startup support
- [x] Ran focused build/test validation, including live LLM coverage and launcher smoke
- [x] Retired completed goals from open-dashboard view and reordered incomplete backlog by priority
- [x] Completed G078–G111 goal track (Edict control plane, intern workers, quality contracts, cognitive libraries, scaffolds, overlay dictionaries, process isolation, Root1 broker, session resume, architectural vision)
- [x] Archived 27 completed goals to `Archive/Goals/` and updated `ARCHIVE_INDEX.md`
- [x] Completed G093: explicit overlay dictionaries with 7 VM opcodes, 8 tests, acceptance-criteria demo, and WP_G093 design note
- [x] Updated Dashboard and README to align with completed work and G075 as the remaining long-arc goal
