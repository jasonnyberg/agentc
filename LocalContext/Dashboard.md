# Dashboard

**Project**: AgentC / J3
**Last Updated**: 2026-07-07

## Current Focus
AgentC/J3 is an internal-alpha persistent cognition runtime. The Edict-resident control-plane track is complete: Edict owns provider/session/tool/context semantics through the curated launcher, provider objects, root/request construction, direct-action wrappers, intern-worker public words, cognitive capability libraries, cognitive skill scaffolds, and overlay dictionaries for reference-scoped ReadOnly sharing. C++ remains the native transport, persistence, credential, provider-adapter, Root1 broker, and lifecycle substrate.

A complete additive TinyCC runtime substrate is now in place through G116, and the G118 code-quality consolidation pass is complete. AgentC exposes `AGENTC_ENABLE_TCC` plus `AGENTC_TCC_ROOT`, consumes a local `~/tinycc` PIC build, compiles and invokes fixed-ABI generated C through `agentc_tcc_worker_exec`, exposes an additive `tcc.*` surface in Edict, and supports isolated helper-exec success / compile-error / missing-entry / runtime-failure / timeout / cancellation / crash envelopes plus explicit symbol allowlisting. All IPC primitives, `BoundSymbol`, symbol-resolution, and opcode-dispatch boilerplate are now consolidated in `edict/pipe_io.h` and `edict/tcc_shared.h`. Cartographer + clang/libffi remain unchanged as the existing native import/reflection path. G117 (AgentC/DeltaGUI market-data hub) is now in progress: Phases 0–3 are implemented and verified in the new separate `markethub/` module (canonical events, pub/sub bus, quote cache, bounded history, highlight engine, GreekScope GEX `extern "C"` façade, isolated TCC bridge demo); Phase 4 (migration path) remains. G075 remains the long-arc speculative Edict architecture goal.

## Incomplete Goals — Priority Order

1. 🔗[G117 — AgentC / DeltaGUI Market Data Hub Integration](./Knowledge/Goals/G117-DeltaGuiBackendToolAdapterExploration/index.md) — **IN PROGRESS**; Phases 0–3 complete and verified 2026-07-07, and the first provider-owned brokerage auth/API request boundary landed 2026-07-09: Phase 0 inventory/schemas (WP_G117), separate `markethub/` module with canonical events, pub/sub bus, quote cache, bounded history, price-move/stale-quote highlight engine, GreekScope GEX `extern "C"` façade, isolated TCC bridge demo, plus TradeStation-preferred/Schwab-supported OAuth and market-data request specs. Module-separation constraint honored: markethub consumes AgentC facilities and is never linked into core targets. Remaining: execute provider request specs in native adapters, ingest live responses into the hub, expose Edict-facing hub config, and migrate DeltaGUI toward hub subscription.
2. 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — **DEFERRED**; long-arc ToT/MCTS/ReAct-style native speculation after a concrete demonstration need appears.

### Blocked
No active blockers. Core TinyCC substrate goals G112-G116 are complete; G118 code-quality consolidation is complete. Latest validation (2026-07-09): `markethub_tests` 17/17 (brokerage provider + TCC/façade build) and full `./build/edict/edict_tests` 210/210. Earlier G117 validation also covered `TccRuntimeTest.*` 12/12, façade-disabled markethub build 9 passed/3 skipped/0 failed, and `AGENTC_ENABLE_TCC=OFF` markethub build 10 passed/2 skipped/0 failed.

## Active Context
- **Completed goals retired and archived**: G074, G078, G079, G080, G084, G085, G086, G087, G088, G089, G090, G091, G092, G093, G094, G095, G096, G097, G098, G099, G100, G101, G102, G103, G104, G105, G106, G107, G108, G109, G110, and G111 are complete and archived in `LocalContext/Knowledge/Archive/Goals/`; see 🔗[Archive Index](./Knowledge/Archive/ARCHIVE_INDEX.md). Older completed goals G068, G071, G072, G073, G076, G077, G081, G082, and G083 were archived in prior passes.

- **TinyCC core track is complete through G116, with G118 consolidation complete**: G112-G116 now have passing code/test evidence; G118 eliminated all IPC, struct, symbol-resolution, and opcode-dispatch duplication. The canonical shared headers are `edict/pipe_io.h` (inline IPC primitives) and `edict/tcc_shared.h` (`BoundSymbol`, TCC constants, `resolveSymbolOrigin`, `closeLibraryHandles`). `edict/CMakeLists.txt` exposes `AGENTC_ENABLE_TCC` and `AGENTC_TCC_ROOT`; `edict/tcc_runtime.*`, `edict/tcc_worker_exec_main.cpp`, `edict/tcc_worker_native.cpp`, and `edict/edict_vm_tcc.cpp` provide the runtime/helper/Edict surface; `edict/tests/tcc_runtime_test.cpp` and `tcc_test_adapter.cpp` verify the path. Cartographer/libffi remains the existing import/reflection mechanism.
- **Local TinyCC build evidence**: `~/tinycc` now supports `./configure --enable-static-pic`, and the working local runtime path uses `./configure --enable-static-pic --with-selinux && make libtcc.a libtcc1.a`. A shared-object probe linked successfully against the local PIC `libtcc.a`, while the system `/usr/lib64/libtcc.a` still fails the same shared-link use as non-PIC. `agentc_tcc_worker_exec` now sets its lib path from `AGENTC_TCC_ROOT`, so local `libtcc1.a` is found at runtime.
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
- **Validation baseline from latest implementation pass**: TinyCC focused coverage now passes `TccRuntimeTest.*` 12/12, including compile error, missing entry symbol, unauthorized symbol relocation failure, helper-exec isolation success, runtime failure, timeout, cancellation, crash containment, and Edict builtin surface coverage. Full `./build/edict/edict_tests` passes 210/210, and a separate `build-no-tcc` configuration still builds `edict_tests` with `AGENTC_ENABLE_TCC=OFF`.
- **G117 markethub module (Phases 0–3 + provider boundary)**: top-level `markethub/` module — standalone `markethub` static library (std-only: `MarketEvent`, `MarketHub` bus, `QuoteCache`, bucketed/bounded `HistoryStore`, `DeltaFeedSubscriber`, `EnvelopeCollectorSubscriber`, `PriceMoveRule`/`StaleQuoteRule`, deterministic fixture ingest, and `BrokerageProvider` auth/API request builders), plus `agentc_greekscope_facade` shared library compiled read-only from `~/DeltaGUI/backend_cpp` GEX sources (configurable via `AGENTC_MARKETHUB_DELTAGUI_ROOT`, auto-disabled when absent, zero DeltaGUI modifications). `markethub_tests` (17 tests) is the only target linking `libedict` — it drives the G116 allowlist + isolated TCC worker bridge demo returning real GEX snapshot JSON and validates TradeStation-preferred/Schwab-supported brokerage request specs. Dependency direction is strictly markethub→AgentC; no core target references markethub. Phase 0 design record: WP_G117_MarketDataHubPhase0Inventory.
- **Documentation direction**: README targets potential users with AgentC's unique value, applications, runnable patterns, maturity expectations, and roadmap. 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) and 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md) remain the key LLM-facing deep references. 🔗[WP — AgentC Architectural Vision & Intern Concurrency Model](./Knowledge/WorkProducts/WP-AgentCArchitecturalVisionInternConcurrency-2026-06-21.md) is the authoritative roadmap reference.
- **Incomplete backlog priority**: current order is G117 → G075. Core TinyCC goals G112-G116 are complete but not yet archived; G117 is in progress with Phases 0–3 and provider auth/API request specs implemented and verified — live-provider execution, Edict-facing hub config, and DeltaGUI subscription migration remain. G075 remains the long-arc speculative-architecture goal.

## Handoff Note

**Project**: AgentC/J3 is an internal-alpha persistent cognition runtime. Edict is the authoritative agent control plane over a C++ persistence/transport/concurrency substrate.

**Current State**: Core TinyCC integration goals G112-G116 are complete, and G118 consolidation is complete. G117 is in progress: Phases 0–3 are implemented and verified in the separate `markethub/` module — canonical event model, pub/sub hub with quote cache and bounded history, price-move/stale-quote highlight engine, two subscriber shapes, GreekScope GEX `extern "C"` façade invoked from isolated TCC workers through the G116 allowlist, and a provider-owned brokerage auth/API request boundary for TradeStation and Schwab. DeltaGUI sources are consumed read-only; nothing in `~/DeltaGUI` was modified.

**Next Action**: G117 Phase 4 when requested: execute the new `BrokerageProvider` request specs in a provider-owned native adapter, normalize live TradeStation-first/Schwab responses into hub events, expose Edict-facing hub configuration/subscription words, and migrate DeltaGUI toward hub subscription. Read `markethub/README.md`, `markethub/include/markethub/brokerage_provider.h`, `markethub/include/markethub/market_hub.h`, and 🔗[WP — G117 Market Data Hub Phase 0 Inventory](./Knowledge/WorkProducts/WP_G117_MarketDataHubPhase0Inventory.md) first. Keep Cartographer/libffi and TinyCC as separate native paths, and avoid touching `~/DeltaGUI` unless a tiny non-breaking seam is genuinely required.

**Key Context**:
- **Latest verification (2026-07-09)**: TDD red compile failure observed for missing `markethub/brokerage_provider.h`; after implementation, `PATH=/usr/bin:/usr/sbin:$PATH /usr/bin/cmake --build build --target markethub_tests -j2 && ./build/markethub/markethub_tests` passes 17/17; full `./build/edict/edict_tests` passes 210/210. Earlier G117 checks: `TccRuntimeTest.*` 12/12, façade-disabled markethub build 9 / skips 3, and `AGENTC_ENABLE_TCC=OFF` markethub build 10 / skips 2.
- **Host linkage reality**: the system `/usr/lib64/libtcc.a` remains non-PIC for `libedict.so`; the supported AgentC path is a dedicated helper executable, not linking libtcc directly into `libedict`.
- **Local development configuration**: `AGENTC_TCC_ROOT=/home/jwnyberg/tinycc` is the live TCC-on setup. `agentc_tcc_worker_exec` now sets its lib path from that root so local `libtcc1.a` is available at runtime.
- **Downstream boundary**: G117's hub lives in the separate top-level `markethub/` module (strict one-way dependency markethub→AgentC; only `markethub_tests` links `libedict`). The bridge rule still holds: use AgentC-side `extern "C"` adapters and G116 allowlisted TCC workers for any legacy DeltaGUI/backend capability before touching `~/DeltaGUI`. The hub must never be linked into or entangled with core AgentC targets.

**Do NOT**: Do not add new intern-specific VM opcodes, do not expose raw fds as durable Edict state, and do not pass stateful provider/runtime handles into intern worker context/imports. Full kill-mid-op activation-frame serialization and stable cross-version code-object identity remain future work.

**G075 activation criteria**: Start G075 when a concrete Tree-of-Thoughts, MCTS, or recursive ReAct demonstration is needed to validate slab snapshot/rollback economics for multi-branch reasoning. The prerequisite control-plane stability (G078/G079/G080) and worker isolation (G091/G093/G107/G110) are now in place. The TinyCC track may later provide native generated evaluators for a G075 demo, but it is not a prerequisite for G075.

## Active Agents
None.

## Knowledge Inventory

### Goals — incomplete priority view (2)
- 🔗[G117 — AgentC / DeltaGUI Market Data Hub Integration](./Knowledge/Goals/G117-DeltaGuiBackendToolAdapterExploration/index.md) — in progress; Phases 0–3 plus provider-owned brokerage auth/API request specs implemented and verified in the separate `markethub/` module (hub MVP, highlight engine, GEX façade + isolated TCC bridge demo, TradeStation-preferred/Schwab-supported request builders); live provider execution and Phase 4 migration path remain.
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — deferred speculative reasoning architecture (ToT/MCTS/ReAct).

### WorkProducts — active references (24)
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
- 🔗[WP — TinyCC Native Interoperability Plan](./Knowledge/WorkProducts/WP_G112_TinyccNativeInteropPlan.md)
- 🔗[WP — G117 Market Data Hub Phase 0 Inventory](./Knowledge/WorkProducts/WP_G117_MarketDataHubPhase0Inventory.md)

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
- [x] Created TinyCC native interop plan/goals from `~/ffi_alternative.txt`: WP_G112 plus G112–G117, preserving Cartographer/libffi as the existing path and defining TinyCC as an additive generated-C adapter mechanism.
- [x] Advanced the TinyCC track from planning into a verified additive implementation slice: local `~/tinycc` PIC build support (`--enable-static-pic`), `AGENTC_ENABLE_TCC`/`AGENTC_TCC_ROOT`, helper-exec libtcc pathing, negative-path TCC tests, focused `TccRuntimeTest.*` 7/7, full `edict_tests` 205/205, and `AGENTC_ENABLE_TCC=OFF` build validation.
- [x] Completed TinyCC core goals G113-G115: added explicit Edict lifecycle/error coverage plus isolated runtime-failure/cancel/crash tests, bringing `TccRuntimeTest.*` to 12/12 and full `edict_tests` to 210/210 while preserving the optional no-TCC build.
- [x] Completed G118 TCC code-quality consolidation: created `edict/pipe_io.h` and `edict/tcc_shared.h`; rewrote `tcc_runtime.cpp`, `tcc_worker_native.cpp`, and `edict_vm_tcc.cpp` to eliminate all IPC, struct, symbol-resolution, and opcode-dispatch duplication; all 7 ACs verified: `TccRuntimeTest.*` 12/12, `edict_tests` 210/210, `build-no-tcc` clean, zero new warnings.
- [x] Redesigned G117 as an AgentC/DeltaGUI market-data hub integration goal with AgentC as pub/sub data hub, DeltaGUI as subscriber/transitional source, and the C ABI/TCC façade retained as the first bridge slice.
- [x] Implemented G117 Phases 0–3: Phase 0 inventory/schema WP; standalone `markethub/` module (canonical events, pub/sub hub, quote cache, bounded history, highlight engine, two subscriber shapes); GreekScope GEX `extern "C"` façade compiled read-only from DeltaGUI sources; G116-allowlisted isolated TCC bridge demo returning GEX snapshot JSON. Verified: markethub_tests 12/12, TccRuntimeTest 12/12, edict_tests 210/210, façade-disabled and no-TCC builds degrade to clean skips.
