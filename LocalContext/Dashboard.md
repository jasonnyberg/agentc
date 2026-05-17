# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-05-17

## Current Focus
AgentC/J3 is an advanced research prototype/internal-alpha moving toward an Edict-resident agent loop: Edict should own provider/session/control-plane semantics while C++ remains the native transport, persistence, credential, and lifecycle substrate.

Completed implementation/documentation slices are retired from the open-dashboard view; the incomplete backlog is listed below in priority order. Work continues under the active parent track 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md): raw Edict now has deterministic `intern_run!`, broker-compatible async `intern_start!` / `intern_sync!` / `intern_cancel!`, cooperative cancellation/backpressure envelopes, the first 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](./Knowledge/Goals/G110-EventfdEpollMicroVmIpcDesign/index.md) substrate, and completed 🔗[G109 — Listree ReadOnly Mutation Surface Hardening](./Knowledge/Goals/G109-ListreeReadOnlyMutationSurfaceHardening/index.md) for public VM/Cursor shared-context safety. The next implementation priority is 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](./Knowledge/Goals/G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md): move remaining intern-specific VM opcode behavior behind importable Root1/worker primitives and plain Edict words before closing remaining G091 lifecycle work and promoting G099 task-quality contracts.

## Incomplete Goals — Priority Order

1. 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — **ACTIVE** parent track; make Edict the authoritative provider/session/tool/context control plane.
2. 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](./Knowledge/Goals/G110-EventfdEpollMicroVmIpcDesign/index.md) — **ACTIVE**; prototype now has participant eventfds, Root1 epoll polling, resource keys, ownership-requested flags, wait queues, grant tokens, fixed-size mailbox descriptors, bounded mailbox ring, file-backed `CoordinationSlab` layout, fd reconstruction after remap, cancellation/backpressure descriptors, and first pidfd owner-death reporting; next is abandoned-resource recovery policy and future `await!` shape.
3. 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](./Knowledge/Goals/G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) — **ACTIVE / NEXT**; first slices added importable LTV worker primitives plus `worker.edict` / `intern.edict`, `intern_sync!` now composes lower-level drain/collect words in Edict, and `worker.edict_active_count!` exposes job-table pressure; remaining work is to expose task-prep/backpressure/cleanup primitives and move validation/envelope/backpressure/cancellation policy into Edict before deleting opcode shims.
4. 🔗[G091 — Intern Worker Concurrency MVP](./Knowledge/Goals/G091-InternWorkerConcurrencyMvp/index.md) — **ACTIVE**; broker-compatible async `intern_start!` / `intern_sync!` / `intern_cancel!` plus cancellation/backpressure first slice have landed, with remaining hardening around worker arena cleanup and the G111 opcode-to-FFI/Edict migration.
5. 🔗[G099 — Intern Task Quality Contracts](./Knowledge/Goals/G099-InternTaskQualityContracts/index.md) — **PLANNED**; minimal bounded/checkable async intern task schemas with waitable/event/backpressure expectations.
6. 🔗[G092 — Cartographer FFI Re-entrancy Metadata](./Knowledge/Goals/G092-CartographerFfiReentrancyMetadata/index.md) — **PLANNED**; capability metadata for worker safety and declarative meta-library loader images.
7. 🔗[G103 — Build-Time Static Core Declaration Image MVP](./Knowledge/Goals/G103-BuildTimeStaticCoreDeclarationImageMvp/index.md) — **PLANNED**; generate/mount a tiny read-only declarative import/module slab image with manifest validation.
8. 🔗[G104 — Immutable Code Object / Activation Frame Split](./Knowledge/Goals/G104-ImmutableCodeObjectActivationFrameSplit/index.md) — **PLANNED**; separate static-shareable bytecode from private mutable execution frames.
9. 🔗[G105 — ReadOnly Static Slab Ownership Model](./Knowledge/Goals/G105-ReadOnlyStaticSlabOwnershipModel/index.md) — **PLANNED**; first immortal/static read-only slab ownership and pin/refcount semantics.
10. 🔗[G108 — Cursor-Scoped Traversal Visit Bitmaps](./Knowledge/Goals/G108-CursorScopedTraversalVisitBitmaps/index.md) — **PLANNED**; traversal-scoped per-slab visit bitmaps for parallel/layer-aware read-only traversal.
11. 🔗[G096 — Authoritative mmap Session Resume](./Knowledge/Goals/G096-AuthoritativeMmapSessionResume/index.md) — **PLANNED**; evolve flat session resume toward layered mmap mounts, broker metadata rehydration, and manifest-validated images.
12. 🔗[G106 — Root1 Slab Advertisement Registry](./Knowledge/Goals/G106-Root1SlabAdvertisementRegistry/index.md) — **PLANNED**; Root1 leases slab ranges/layers and advertises immutable worker publications using broker-compatible resource keys/epochs.
13. 🔗[G107 — Process-Isolated Micro-VM Interns](./Knowledge/Goals/G107-ProcessIsolatedMicroVmInterns/index.md) — **PLANNED**; process-isolated interns mmap static core slabs and communicate through brokered async mailboxes plus optional publications.
14. 🔗[G101 — Direct Edict Tool-Emission Path](./Knowledge/Goals/G101-EdictDirectToolEmissionPath/index.md) — **PLANNED**; guarded compact model-emitted Edict as an alternative to JSON tool-call glue.
15. 🔗[G100 — Edict Isolation Contract Hardening](./Knowledge/Goals/G100-EdictIsolationContractHardening/index.md) — **PLANNED**; test/document isolated-call safety for LLM-generated Edict.
16. 🔗[G094 — Curated Native Cognitive Capability Libraries](./Knowledge/Goals/G094-CuratedNativeCognitiveLibraries/index.md) — **PLANNED**; structural diff, AST/tree-sitter, and persistent knowledge-graph capability surfaces.
17. 🔗[G095 — Edict Cognitive Skill Scaffolds](./Knowledge/Goals/G095-EdictCognitiveSkillScaffolds/index.md) — **PLANNED**; reusable investigation, code-review, and refactoring-planner scaffolds.
18. 🔗[G097 — Composite Speculation + Logic + FFI Demo](./Knowledge/Goals/G097-CompositeSpeculationLogicFfiDemo/index.md) — **PLANNED**; showcase speculation + miniKanren + FFI + persistence composition.
19. 🔗[G098 — Architectural Vision Work Product](./Knowledge/Goals/G098-ArchitecturalVisionWorkProduct/index.md) — **PLANNED**; preserve architectural/intern-concurrency vision as a durable WorkProduct.
20. 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — **DEFERRED**; revisit ToT/MCTS/ReAct-style native speculation after loop/tool/context stability.
21. 🔗[G093 — Reference-Scoped ReadOnly Sharing](./Knowledge/Goals/G093-ReferenceScopedReadOnlySharing/index.md) — **DEFERRED**; finer-grained ReadOnly/shadowing after whole-subtree sharing proves insufficient.

### Blocked
None.

## Active Context
- **Completed goals retired from open view**: G074, G079, G080, G084, G085, G086, G087, G088, G089, G090, G102, and G109 are complete and no longer listed in the incomplete-priority backlog. Older completed goals G068, G071, G072, G073, G076, G077, G081, G082, and G083 were moved to `LocalContext/Knowledge/Archive/Goals/`; see 🔗[Archive Index](./Knowledge/Archive/ARCHIVE_INDEX.md). Completed-but-unarchived goals remain archive candidates for the next archival cleanup.
- **Edict provider surface**: `llm.init(name)` returns stable provider objects. Current request pattern is `provider < [prompt] request! > / /`; launcher-backed REPL pattern is `provider < repl! > / /`. G080 added provider-owned `context_reset!` / `context_inspect!` plus REPL slash commands `/reset`, `/clear`, `/context`, and `/inspect`.
- **Intern worker / micro-VM substrate**: raw Edict now has `intern_run!`, `intern_start!`, `intern_sync!`, and `intern_cancel!`. `intern_run!` accepts a bounded task envelope, freezes shared `context`/`imports`, snapshots `input`, runs deterministic Edict source in a fresh worker VM with private `workspace`, joins, and returns a structured result copied back on the coordinator thread. `intern_start!` launches the same bounded worker asynchronously through an Edict-local `InternJobManager` using G110 broker-compatible job ids/waitables/descriptors; `intern_sync!` drains events and returns running/cancel-requested/final structured status on the coordinator thread; `intern_cancel!` is a plain bootstrap Edict word over `intern_sync!` and causes final sync to return `cancelled` without merging worker output. `max_active_jobs` on the task envelope provides first backpressure coverage. New 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](./Knowledge/Goals/G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) records the next architectural step: keep only eventfd/epoll/pidfd/mmap/thread/fresh-worker-VM primitives in importable C/C++ libraries and move intern validation, envelope shaping, cancellation/backpressure policy, and public words into Edict modules. Its first slices now expose LTV worker primitives from `libedict.so`, wrap them in `worker.edict`, define module-backed `intern.edict` public words, split module-backed `intern_sync!` into Edict-level `worker.edict_drain_events!` plus `worker.edict_collect!` composition, and add `worker.edict_active_count!` while retaining VM opcode shims for compatibility. 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](./Knowledge/Goals/G110-EventfdEpollMicroVmIpcDesign/index.md) is active and now has a first C++ prototype in `core/root1_resource_broker.*`: per-participant eventfds, Root1 epoll polling, `ResourceKey`s, an atomic ownership/contended word, wait queues, grant tokens, fixed-size mailbox descriptors, bounded SPSC-style mailbox rings, file-backed/anonymous `CoordinationSlab` placement for participant mailboxes plus resource state words, fd reconstruction after remap, cancellation/backpressure descriptors, and first pidfd owner-death reporting. Safety audit 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](./Knowledge/WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md) found current traversal visited state is already external and a frozen-tree removal gap; completed 🔗[G109](./Knowledge/Goals/G109-ListreeReadOnlyMutationSurfaceHardening/index.md) now hardens public VM/Cursor paths so recursively frozen shared context refuses assignment, path removal, remove-head, list pop, and cleanup-pruning mutations. Longer-term memory-substrate concept: 🔗[Layered mmap Micro-VM Architecture](./Knowledge/Concepts/LayeredMmapMicroVmArchitecture/index.md), now refined around build-time Root0 static slab-image generation, runtime Root1 resource-broker/slab-directory ownership, Root1-brokered mutable coordination slabs, declarative meta-library import images, low-upward core vs high-downward micro-VM slab allocation, immortal/static read-only slab ownership, content-addressed core images, and hybrid mailbox/JSON/bulk-slab communication. See 🔗[K032 — Edict Intern Worker Surface](./Knowledge/Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md).
- **Curated launcher**: `./edict.sh` injects `EDICT_PATH`, preloads `agentc_curated.edict`, configures the `llm` bootstrap surface, and defaults to `EDICT_AUTO_CHAT=1` with `EDICT_DEFAULT_PRESET=local-qwen`. Set `EDICT_AUTO_CHAT=0` for raw curated Edict execution.
- **OpenAI Codex provider**: `openai-codex` is live through pi ChatGPT OAuth credentials in `~/.pi/agent/auth.json`; default model is `gpt-5.3-codex` with low reasoning effort. Live smoke test returned `ok`. Lower attempted Codex model names were unsupported for the account.
- **Important VM semantics now fixed/documented**: concatenated prefix sigils apply to the same identifier left-to-right (`/@name`, `//name`, etc.); leading `-name` selects the tail/oldest dictionary value for lookup, assignment, and removal; strict/lax unresolved lookup modes exist (`lax!`, `strict!`, `strict_null!`, `strict_fail!`); bare `/` is the documented stack discard and `pop` is no longer a compiler/bootstrap alias.
- **Raw Edict named sessions (G102)**: `./build/edict/edict --session ID` creates/resumes a root scope under `/tmp/session/<id>/` by default; `--session-base DIR` or `EDICT_SESSION_BASE` changes the base. Session ids are filesystem-safe (`[A-Za-z0-9._-]+`, excluding `.`/`..`). This uses the current session-image/slab store on normal process exit; full kill-mid-turn authoritative mmap resume remains G096.
- **Tool surface landed (G079)**: `extensions/agentc_stdlib` now exposes JSON-envelope file read/write/exact-replace and shell execution helpers. `agentc.edict` wraps them as `agentc_file_read!`, `agentc_file_write!`, `agentc_file_replace!`, `agentc_shell!`, and `agentc_tools`; `llm.edict` attaches `agentc_tools` as `provider.tools` for provider-context use.
- **Streaming surface landed (G074)**: `Runtime::stream_request_json(...)` now launches a detached provider worker that pushes text deltas into `StreamManager`. `agentc_runtime_stream_sync_json(...)` returns an `ok`/`complete` JSON envelope, `agentc.edict` wraps it as `agentc_call_stream!` / `agentc_stream_sync!`, and `llm.edict` provider objects expose `stream_start` / `stream_sync`. Live Google/Gemma smoke with `gemma-4-31b-it` returned `ok`.
- **Validation baseline from latest implementation pass**: G109/G091/G110/G111 validation passed with `cmake --build build --target reflect_tests edict_tests cpp_agent_tests -j2`, `./build/listree/listree_tests --gtest_brief=1` 71/71, `./build/tests/reflect_tests --gtest_brief=1` 43/43, `./build/edict/edict_tests --gtest_filter='InternWorkerTest.*'` 8/8, focused Edict regression slice `FreezeBuiltin.*:InternWorkerTest.*:EdictVM.*:VMStackTest.*:SimpleAssignTest.*:RegressionMatrixTest.*:PiSimulationTest.MiniKanrenLogicExample` 54/54, and full `./build/cpp-agent/cpp_agent_tests --gtest_brief=1` 49/49. Full unfiltered `edict_tests` currently reports legacy Cartographer/closure callback failures outside the maintained focused slice. Earlier focused pop-transition tests passed 19/19; focused cpp-agent Edict/LLM suite passes 21/21 including live Google/Gemma coverage and G080 REPL context management.
- **Documentation direction**: README now targets potential users with AgentC's unique value, applications, runnable patterns, maturity expectations, and roadmap. 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) and 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md) remain the key LLM-facing deep references. Next documentation improvement is a live notebook style with executable examples, especially for FFI/Cartographer.
- **Incomplete backlog priority**: current order is G078, G110, G111, G091, G099, G092, G103, G104, G105, G108, G096, G106, G107, G101, G100, G094, G095, G097, G098, G075, G093. The micro-VM sequence now front-loads the Root1 eventfd/epoll resource broker and the G111 opcode-to-FFI/Edict migration because they determine async intern IPC, waitable mailboxes, resource ownership, process isolation, Root1 slab-directory semantics, and how much implementation stays in Edict rather than VM dispatch. G091 async interns are broker-compatible and G109 has hardened shared read-only context; G103–G108/G096/G106/G107 then build the static-core, immutable-code, read-only-slab, traversal, layered-resume, publication, and process-isolation substrate. G093 remains deliberately deferred until whole-subtree readonly core/published layers prove insufficient.

## Handoff Note

**Current State**: The foundational persistence/client-host work has been retired to the archive. The live project is now centered on G078: making Edict the authoritative control plane for the agent loop. The codebase has a working curated launcher, Edict-side provider objects, provider REPL, local/Codex/Google-Gemma provider paths, provider-owned reset/inspect context management, deterministic `intern_run!`, broker-compatible async `intern_start!` / `intern_sync!` / `intern_cancel!`, cooperative cancellation/backpressure envelopes, hardened public ReadOnly assignment/removal guards for shared worker context, first file/shell tool surface, first real-time ghost-queue stream surface, strict lookup modes, corrected prefix-chain semantics, and corrected tail-history semantics.

**Next Action**: Continue 🔗[G111](./Knowledge/Goals/G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) beyond the transitional module-backed worker primitive/drain-collect/active-count slice: expose task-preparation, explicit backpressure/capacity checks, cleanup/drop, and eventually Root1 participant/mailbox primitives so validation, envelope shaping, cancellation, and backpressure policy can move into `intern.edict`, then delete the intern VM opcode shims. After that, close remaining 🔗[G091](./Knowledge/Goals/G091-InternWorkerConcurrencyMvp/index.md) lifecycle cleanup and promote 🔗[G099](./Knowledge/Goals/G099-InternTaskQualityContracts/index.md) using the Edict-level contract helpers. Keep 🔗[G110](./Knowledge/Goals/G110-EventfdEpollMicroVmIpcDesign/index.md) open for abandoned-resource recovery and future `await!` semantics.

**Key Constraints**: Preserve stable provider-object in-place mutation; avoid expensive Codex `gpt-5.5`; keep Codex default reasoning low; use explicit scalar success/failure sentinels for `& |` control flow; keep Listree mutations on the VM/main thread for any future streaming architecture.

## Active Agents
None.

## Knowledge Inventory

### Goals — incomplete priority view (21)
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — active parent track.
- 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](./Knowledge/Goals/G110-EventfdEpollMicroVmIpcDesign/index.md) — active design/prototype track for brokered waitables, resource ownership, and mailbox IPC.
- 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](./Knowledge/Goals/G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) — planned migration from intern VM opcodes to importable native primitives plus Edict module policy.
- 🔗[G091 — Intern Worker Concurrency MVP](./Knowledge/Goals/G091-InternWorkerConcurrencyMvp/index.md) — active local-intern worker architecture slice; async `intern_start!` / `intern_sync!` / `intern_cancel!` and backpressure landed, worker lifecycle cleanup plus G111 migration remain.
- 🔗[G099 — Intern Task Quality Contracts](./Knowledge/Goals/G099-InternTaskQualityContracts/index.md) — planned minimal bounded/checkable async intern contracts.
- 🔗[G092 — Cartographer FFI Re-entrancy Metadata](./Knowledge/Goals/G092-CartographerFfiReentrancyMetadata/index.md) — planned FFI/capability metadata for worker safety and meta-library declarations.
- 🔗[G103 — Build-Time Static Core Declaration Image MVP](./Knowledge/Goals/G103-BuildTimeStaticCoreDeclarationImageMvp/index.md) — planned static declaration image generator/mount slice.
- 🔗[G104 — Immutable Code Object / Activation Frame Split](./Knowledge/Goals/G104-ImmutableCodeObjectActivationFrameSplit/index.md) — planned static bytecode/private frame split.
- 🔗[G105 — ReadOnly Static Slab Ownership Model](./Knowledge/Goals/G105-ReadOnlyStaticSlabOwnershipModel/index.md) — planned immortal/static read-only slab ownership slice.
- 🔗[G108 — Cursor-Scoped Traversal Visit Bitmaps](./Knowledge/Goals/G108-CursorScopedTraversalVisitBitmaps/index.md) — planned traversal-scoped slab bitmap state.
- 🔗[G096 — Authoritative mmap Session Resume](./Knowledge/Goals/G096-AuthoritativeMmapSessionResume/index.md) — planned layered mmap mount/session resume hardening.
- 🔗[G106 — Root1 Slab Advertisement Registry](./Knowledge/Goals/G106-Root1SlabAdvertisementRegistry/index.md) — planned Root1 publication/range registry using broker-compatible resource keys/epochs.
- 🔗[G107 — Process-Isolated Micro-VM Interns](./Knowledge/Goals/G107-ProcessIsolatedMicroVmInterns/index.md) — planned process-isolated intern workers over static core slabs and brokered mailboxes.
- 🔗[G101 — Direct Edict Tool-Emission Path](./Knowledge/Goals/G101-EdictDirectToolEmissionPath/index.md) — planned guarded direct Edict tool-use path.
- 🔗[G100 — Edict Isolation Contract Hardening](./Knowledge/Goals/G100-EdictIsolationContractHardening/index.md) — planned test/doc hardening for isolated LLM-emitted Edict.
- 🔗[G094 — Curated Native Cognitive Capability Libraries](./Knowledge/Goals/G094-CuratedNativeCognitiveLibraries/index.md) — planned structural diff, AST/tree-sitter, and knowledge-graph capabilities.
- 🔗[G095 — Edict Cognitive Skill Scaffolds](./Knowledge/Goals/G095-EdictCognitiveSkillScaffolds/index.md) — planned investigation/review/refactor scaffolds.
- 🔗[G097 — Composite Speculation + Logic + FFI Demo](./Knowledge/Goals/G097-CompositeSpeculationLogicFfiDemo/index.md) — planned integrated showcase/demo.
- 🔗[G098 — Architectural Vision Work Product](./Knowledge/Goals/G098-ArchitecturalVisionWorkProduct/index.md) — planned durable work product for the extracted architecture summary.
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — deferred speculative reasoning architecture.
- 🔗[G093 — Reference-Scoped ReadOnly Sharing](./Knowledge/Goals/G093-ReferenceScopedReadOnlySharing/index.md) — deferred finer-grained ReadOnly/shadowing refinement.

### WorkProducts — active references (13)
- 🔗[AgentLang](./Knowledge/WorkProducts/AgentLang.md)
- 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md)
- 🔗[WP — C++ Agent Runtime File Structure](./Knowledge/WorkProducts/WP_CppAgentRuntimeFileStructure.md)
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
- [x] Created/updated G084–G110 goal files
- [x] Implemented VM/compiler cleanup, VM translation-unit split, live Google/Gemma test coverage, adjacent-eval documentation/script style update, README rewrites, `pop` keyword removal, architectural/intern-concurrency goal extraction, and raw Edict named-session startup support
- [x] Ran focused build/test validation, including live LLM coverage and launcher smoke; README example/link/style, pop-removal inventory, session CLI, and session-state regression checks passed
- [x] Retired completed goals from open-dashboard view and reordered incomplete backlog by priority
- [x] Completed G080 provider-owned LLM REPL context reset/inspect slice and began G091 with the deterministic `intern_run!` worker dispatch slice
- [x] Updated full-send slab priority plan and added G103–G110 planning goals
- [x] Began G110 implementation with the first Root1 eventfd/epoll resource-broker prototype and regression tests
- [x] Advanced G091 with broker-compatible raw Edict `intern_start!` / `intern_sync!` and focused async intern coverage
- [x] Completed G109 Listree ReadOnly mutation-surface hardening and focused shared-context removal coverage
- [x] Advanced G091 with cooperative `intern_cancel!`, `max_active_jobs` backpressure, and focused async event coverage
- [x] Added G111 migration plan for Root1/worker primitive FFI libraries plus Edict-owned intern words
- [x] Landed first G111 implementation slice with importable LTV worker primitives, `worker.edict` / `intern.edict`, and module-backed intern regression coverage
- [x] Split module-backed `intern_sync!` into Edict-level drain/collect composition over lower-level worker primitive words
- [x] Updated Dashboard
- [x] Updated Timeline
