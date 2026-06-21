# Dashboard

**Project**: AgentC / J3
**Last Updated**: 2026-06-21

## Current Focus
AgentC/J3 is an internal-alpha persistent cognition runtime with the parent Edict-resident control-plane track now complete for the current milestone: Edict owns provider/session/tool/context semantics through the curated launcher, provider objects, root/request construction, direct-action wrappers, intern-worker public words, and cognitive capability libraries; C++ remains the native transport, persistence, credential, provider-adapter, Root1 broker, and lifecycle substrate. G078 and G110 are complete and retired from the open-dashboard view. Remaining prioritized work starts at G095 cognitive skill scaffolds, followed by G097/G098 showcase/documentation work.

## Incomplete Goals — Priority Order

1. 🔗[G095 — Edict Cognitive Skill Scaffolds](./Knowledge/Goals/G095-EdictCognitiveSkillScaffolds/index.md) — **PLANNED**; reusable investigation, code-review, and refactoring-planner scaffolds.
2. 🔗[G097 — Composite Speculation + Logic + FFI Demo](./Knowledge/Goals/G097-CompositeSpeculationLogicFfiDemo/index.md) — **PLANNED**; showcase speculation + miniKanren + FFI + persistence composition.
3. 🔗[G098 — Architectural Vision Work Product](./Knowledge/Goals/G098-ArchitecturalVisionWorkProduct/index.md) — **PLANNED**; preserve architectural/intern-concurrency vision as a durable WorkProduct.
4. 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — **DEFERRED**; revisit ToT/MCTS/ReAct-style native speculation after loop/tool/context stability.
5. 🔗[G093 — Reference-Scoped ReadOnly Sharing](./Knowledge/Goals/G093-ReferenceScopedReadOnlySharing/index.md) — **DEFERRED**; finer-grained ReadOnly/shadowing after whole-subtree sharing proves insufficient.


### Blocked
No active blockers. Full `edict_tests` green (186/186). `treesitter_tests` 28/28 (15 bridge + 6 diff + 7 KG).

## Active Context
- **Completed goals retired from open view**: G074, G078, G079, G080, G084, G085, G086, G087, G088, G089, G090, G091, G092, G094, G096, G099, G100, G101, G102, G103, G104, G105, G106, G107, G108, G109, G110, and G111 are complete and no longer listed in the incomplete-priority backlog. Older completed goals G068, G071, G072, G073, G076, G077, G081, G082, and G083 were moved to `LocalContext/Knowledge/Archive/Goals/`; see 🔗[Archive Index](./Knowledge/Archive/ARCHIVE_INDEX.md). Completed-but-unarchived goals remain archive candidates for the next archival cleanup.
- **Edict provider surface**: `llm.init(name)` returns stable provider objects. Current request pattern is `provider < [prompt] request! > / /`; launcher-backed REPL pattern is `provider < repl! > / /`. G080 added provider-owned `context_reset!` / `context_inspect!` plus REPL slash commands `/reset`, `/clear`, `/context`, and `/inspect`.
- **Intern worker / micro-VM substrate**: module-backed Edict now has `intern_run!`, `intern_start!`, `intern_sync!`, and `intern_cancel!` via imported worker primitives and `worker.edict` / `intern.edict`; raw VM startup no longer installs these as bootstrap opcodes. `intern_run!` accepts a bounded task envelope, freezes shared `context`/`imports`, snapshots `input`, runs deterministic Edict source in a fresh worker VM with private `workspace`, joins, and returns a structured result copied back on the coordinator thread. `intern_start!` launches the same bounded worker asynchronously through an Edict-local `InternJobManager` using G110 broker-compatible job ids/waitables/descriptors; `intern_sync!` drains events and returns running/cancel-requested/final structured status on the coordinator thread; `intern_cancel!` is a plain module-backed Edict word and causes final sync to return `cancelled` without merging worker output when the request is accepted before terminal completion. `max_active_jobs` on the task envelope provides first backpressure coverage. G091 lifecycle cleanup now retains terminal async statuses for repeated sync until explicit `worker.edict_drop!` or bounded retention sweep, counts only non-terminal/non-abandoned jobs as active, reports running drops as `state: "abandoned"`, reports terminal drops as `state: "dropped"`, suppresses abandoned completion mailbox publication, and treats cancellation as non-retroactive after terminal completion. Completed 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](./Knowledge/Goals/G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) moved intern VM-opcode behavior behind importable worker primitives and plain Edict public words. It exposes LTV worker primitives from `libedict.so`, wraps them in `worker.edict`, defines module-backed `intern.edict` public words, splits module-backed `intern_run!` into prepare/run-status/Edict run-envelope composition, splits module-backed `intern_start!` into prepare/capacity-status/backpressure-envelope/start-status/Edict start-envelope composition, splits module-backed `intern_sync!` into Edict-level `worker.edict_drain_events!` plus `worker.edict_collect_status!` plus `intern.sync_envelope!` composition, splits module-backed `intern_cancel!` into `worker.edict_request_cancel!` plus `worker.edict_collect_status!` plus `intern.sync_envelope!`, adds `worker.edict_active_count!`, adds `worker.edict_drop!` explicit cleanup, moves public envelope helpers into `intern.edict`, adds first generic `root1.edict` wrappers for participant registration, polling, mailbox send/drain, cancellation, and backpressure descriptors, deletes the intern VM opcode shims and legacy raw native envelope service paths, splits fresh worker-VM execution into `edict_worker_runtime.{h,cpp}`, and splits thin worker C ABI/LTV wrappers into `edict_worker_primitives_ffi.cpp`. 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](./Knowledge/Goals/G110-EventfdEpollMicroVmIpcDesign/index.md) is active and now has a first C++ prototype in `core/root1_resource_broker.*`: per-participant eventfds, Root1 epoll polling, `ResourceKey`s, an atomic ownership/contended word, wait queues, grant tokens, fixed-size mailbox descriptors, bounded SPSC-style mailbox rings, file-backed/anonymous `CoordinationSlab` placement for participant mailboxes plus resource state words, fd reconstruction after remap, cancellation/backpressure descriptors, pidfd owner-death reporting with participant lease recovery, known-resource abandoned-owner recovery, deterministic lease/expiry recovery plus heartbeat renewal for registered resources, first logical Root1 resource/lease wrappers, first module-backed `root1.await!` that polls a logical waitable and drains descriptors without VM continuation parking yet, `EdictVM::resume()` as the first VM hook for yielded continuation resumption, and first in-process `Root1AwaitScheduler` parking/resume table with logical continuation handles/status instead of raw VM-pointer parking records. Safety audit 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](./Knowledge/WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md) found current traversal visited state is already external and a frozen-tree removal gap; completed 🔗[G109](./Knowledge/Goals/G109-ListreeReadOnlyMutationSurfaceHardening/index.md) now hardens public VM/Cursor paths so recursively frozen shared context refuses assignment, path removal, remove-head, list pop, and cleanup-pruning mutations. Longer-term memory-substrate concept: 🔗[Layered mmap Micro-VM Architecture](./Knowledge/Concepts/LayeredMmapMicroVmArchitecture/index.md), now refined around build-time Root0 static slab-image generation, runtime Root1 resource-broker/slab-directory ownership, Root1-brokered mutable coordination slabs, declarative meta-library import images, low-upward core vs high-downward micro-VM slab allocation, immortal/static read-only slab ownership, content-addressed core images, and hybrid mailbox/JSON/bulk-slab communication. See 🔗[K032 — Edict Intern Worker Surface](./Knowledge/Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md).
- **Curated launcher**: `./edict.sh` injects `EDICT_PATH`, imports global `ext`/`runtimeffi` bindings for curated `agentc.edict` wrappers, preloads `agentc_curated.edict`, configures the `llm` bootstrap surface, and defaults to `EDICT_AUTO_CHAT=1` with `EDICT_DEFAULT_PRESET=local-qwen`. Set `EDICT_AUTO_CHAT=0` for raw curated Edict execution.
- **OpenAI Codex provider**: `openai-codex` is live through pi ChatGPT OAuth credentials in `~/.pi/agent/auth.json`; default model is `gpt-5.3-codex` with low reasoning effort. Live smoke test returned `ok`. Lower attempted Codex model names were unsupported for the account.
- **Important VM semantics now fixed/documented**: concatenated prefix sigils apply to the same identifier left-to-right (`/@name`, `//name`, etc.); leading `-name` selects the tail/oldest dictionary value for lookup, assignment, and removal; strict/lax unresolved lookup modes exist (`lax!`, `strict!`, `strict_null!`, `strict_fail!`); bare `/` is the documented stack discard and `pop` is no longer a compiler/bootstrap alias.
- **Raw Edict named sessions (G102)**: `./build/edict/edict --session ID` creates/resumes a root scope under `/tmp/session/<id>/` by default; `--session-base DIR` or `EDICT_SESSION_BASE` changes the base. Session ids are filesystem-safe (`[A-Za-z0-9._-]+`, excluding `.`/`..`). This uses the authoritative session-image/slab store on normal process exit; G096 now covers deterministic root/scheduler/static-mount resume, while full kill-mid-op activation-frame resurrection remains future work.
- **Tool surface landed (G079/G101)**: `extensions/agentc_stdlib` exposes JSON-envelope file read/write/exact-replace and shell execution helpers. `agentc.edict` wraps them as `agentc_file_read!`, `agentc_file_write!`, `agentc_file_replace!`, `agentc_shell!`, and `agentc_tools`; `llm.edict` attaches `agentc_tools` as `provider.tools` for provider-context use. G101 adds `agentc_direct_action!` with an MVP `read_file` whitelist and denied-operation envelopes for compact model-emitted direct Edict actions.
- **Streaming surface landed (G074)**: `Runtime::stream_request_json(...)` now launches a detached provider worker that pushes text deltas into `StreamManager`. `agentc_runtime_stream_sync_json(...)` returns an `ok`/`complete` JSON envelope, `agentc.edict` wraps it as `agentc_call_stream!` / `agentc_stream_sync!`, and `llm.edict` provider objects expose `stream_start` / `stream_sync`. Live Google/Gemma smoke with `gemma-4-31b-it` returned `ok`.
- **Validation baseline from latest implementation pass**: G091/G099 closeout passed focused Intern/Root1/Await validation 39/39, `cmake --build build --target reflect_tests edict_tests cpp_agent_tests treesitter_tests -j2`, full `edict_tests` 186/186, full `reflect_tests` 55/55, `listree_tests` 83/83, `cartographer_tests` 52/52, full `cpp_agent_tests` 56/56, and `treesitter_tests` 28/28.
- **Documentation direction**: README now targets potential users with AgentC's unique value, applications, runnable patterns, maturity expectations, and roadmap. 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) and 🔗[Edict Language Reference](./Knowledge/WorkProducts/edict_language_reference.md) remain the key LLM-facing deep references. Next documentation improvement is a live notebook style with executable examples, especially for FFI/Cartographer.
- **G110 scheduler persistence and await! builtin (2026-06-06)**: `Root1AwaitScheduler::saveState()`/`loadState()` landed — continuation table serialization proven with 8 deterministic resume tests. `await!` is now a real `VMOP_AWAIT` opcode with per-waitable stack argument (`job.waitable await! @events events`) and coordinator default (`await! @events events`). `EdictREPL` carries a scheduler pump callback (all 5 modes). CLI `./edict --session` auto-persists/restores scheduler state. LLM's Guide to Edict updated with yield!/await! patterns, stack-based argument principle, and stale source path fixes. Validation: `Root1AwaitSchedulerTest.*` 11/11, focused edict 86/86, listree 83/83. Commits a321afc through 78566e4.
- **Incomplete backlog priority**: current order is G095, G097, G098, G075, G093. G078, G091, G094, G099, and G110 are COMPLETE and retired from the priority backlog. The Root1 eventfd/epoll broker, Edict-resident parent control-plane substrate, intern-worker MVP, and first quality-contract MVP are closed for their current scopes. G093 remains deliberately deferred until whole-subtree readonly core/published layers prove insufficient.

## Handoff Note

**Project**: AgentC/J3 is an internal-alpha persistent cognition runtime moving toward Edict as the authoritative agent control plane over a C++ persistence/transport/concurrency substrate.

**Current State**: G078 is complete — Edict is the authoritative control-plane surface for provider/session/tool/context semantics through `llm.edict` provider objects, `provider < repl! > / /`, Edict request construction, direct-action wrappers, module-backed intern words, and G094 cognitive capability libraries. The final G078 cleanup removed the C++ provider-contract mirror from `agent_root_vm_ops.cpp`: rehydration now fills provider defaults/contracts from `agentc_provider_contracts.edict`, and the cold-start root skeleton keeps `provider_contract: null` until Edict rehydrates it on the actual session VM. G110 is complete for its design/prototype scope — Root1 has eventfd/epoll participants, mailbox descriptors/rings, resource keys/grants, fd reconstruction, cancellation/backpressure, pidfd owner death, lease/heartbeat recovery, logical Root1 primitives, `EdictVM::resume()`, `Root1AwaitScheduler`, public `await!`, scheduler save/load, CLI/REPL pump wiring, and G106 publication-registry integration. Full activation-frame resurrection, wall-clock timer policy, and process-worker heartbeat cadence are explicitly future production hardening, not open G110 acceptance criteria.

**Next Action**: Advance to 🔗[G095 — Edict Cognitive Skill Scaffolds](./Knowledge/Goals/G095-EdictCognitiveSkillScaffolds/index.md): reusable investigation, code-review, and refactoring-planner scaffolds over the now-complete Edict control-plane, intern-worker, and quality-contract substrate. G078/G091/G099/G110/G094 are complete; full baseline validation is green.

**Post-G106 TODO Sequence**:
1. [x] Completed 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) and 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](./Knowledge/Goals/G110-EventfdEpollMicroVmIpcDesign/index.md): provider/root contracts now resolve from Edict catalog data, Root1 design/prototype ACs are closed, and both goals are retired from the incomplete backlog.
2. [x] Completed 🔗[G101 — Direct Edict Tool-Emission Path](./Knowledge/Goals/G101-EdictDirectToolEmissionPath/index.md): guarded compact model-emitted Edict action dispatcher with `read_file` whitelist, denied-operation envelopes, helper-equivalence tests, and WP_G101 documentation.
3. [x] Completed 🔗[G100 — Edict Isolation Contract Hardening](./Knowledge/Goals/G100-EdictIsolationContractHardening/index.md): isolated-call parent-binding protection, non-leaking thunk locals, explicit context-mutation test, and LLM guide/WorkProduct documentation.
4. [x] Root-caused and fixed ReproFFI/Callback full-suite blocker: (a) `evalDispatchFFI` now allows parser-mapped functions without `resolution_status`, (b) `op_SPLICE` skips auto-resolve for empty strings preventing local scope corruption, (c) regression test `SpliceEmptyStringDoesNotCorruptLocalScope` added.
5. [x] Implemented G094.1–G094.5 (complete): Edict tree-sitter grammar, C++ tree-sitter bridge, Edict builtins (`treesitter.load`/`parse`/`list`/`diff`), structural diff engine, and persistent knowledge graph (`kgraph.create`/`add_node`/`add_edge`/`get_node`/`query`/`nodes`/`edges`). Full `edict_tests` 186/186, `treesitter_tests` 28/28.
6. [ ] Continue 🔗[G095 — Edict Cognitive Skill Scaffolds](./Knowledge/Goals/G095-EdictCognitiveSkillScaffolds/index.md): investigation/review/refactor scaffolds.
7. [ ] Continue showcase/documentation goals in order: 🔗[G097](./Knowledge/Goals/G097-CompositeSpeculationLogicFfiDemo/index.md), 🔗[G098](./Knowledge/Goals/G098-ArchitecturalVisionWorkProduct/index.md), then deferred 🔗[G075](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) / 🔗[G093](./Knowledge/Goals/G093-ReferenceScopedReadOnlySharing/index.md) only after loop/tool/context stability warrants them.

**Key Context**:
- **G078, G091, G099, G110, G094, G101, G100, G106, G096, and ReproFFI/Callback blocker fix are complete.** G091 closed the intern-worker MVP (blocking/async worker surface, lifecycle/drop/abandon, cancellation checkpoints, private cleanup boundary); G099 closed the task quality-contract MVP (bounded task schemas, examples, dispatch rejection, evidence/confidence result validators). Next implementation target is G095 scaffolds.
- **G100 is now complete.** `CallIsolationTest.*` now proves isolated calls cannot overwrite/leak parent bindings while `ctx < ... > /` still deliberately mutates target objects; the LLM guide documents current-syntax safety examples and compact direct-action limits. 📄[WP — G100 Edict Isolation Contract Hardening](./Knowledge/WorkProducts/WP_G100_EdictIsolationContractHardening.md). Validation: CallIsolationTest 8/8, full edict_tests 174/174 (ReproFFI/Callback blocker resolved).
- **G101 is now complete.** `agentc_direct_action!` gives curated launcher sessions a compact guarded direct-action dispatcher. MVP whitelist: `read_file` only, backed by `agentc_file_read!`; unsafe ops such as `shell` return `direct_action_denied` envelopes. 📄[WP — G101 Direct Edict Tool-Emission Path](./Knowledge/WorkProducts/WP_G101_DirectEdictToolEmissionPath.md). Validation: DirectEdictAction 2/2, EdictAgentcModuleTest 6/6, EdictLlmModuleTest 11/11, full cpp_agent_tests 55/55.
- **G106 is now complete.** `Root1ResourceBroker` owns logical publication layer leases plus immutable read-only publication metadata: `leasePublicationLayer`, `registerPublication`, `lookupPublication`, `retirePublication`, and expired-lease withdrawal. Publication registration now opens the manifest file, verifies `fnv1a64:<16-hex>` content hash, and checks layer/epoch/root metadata before visibility. 📄[WP — G106 Root1 Slab Advertisement Registry](./Knowledge/WorkProducts/WP_G106_Root1SlabAdvertisementRegistry.md). Validation: focused G106 publication tests 4/4, Root1 broker 22/22, reflect 55/55, affected Edict Root1/static-image 26/26.
- **G096 is now complete.** Authoritative mmap session resume delivered deterministic root/session restore, file-backed slab[0] persistence, scheduler continuation-table persistence, preserved `static_mounts`, layered static declaration-image restore into `staticBases`, and 📄[WP — G096 Authoritative mmap Session Resume Boundary](./Knowledge/WorkProducts/WP_G096_AuthoritativeMmapSessionResume.md). Validation: focused G096.3 1/1, affected persistence/embedded slice 21/21, full `cpp_agent_tests` 53/53.
- **G108 is now complete.** Cursor-Scoped Traversal Visit Bitmaps: replaced `TraversalContext` (two `std::unordered_set<SlabId>`) with `TraversalVisitState` using per-slab bitmaps (`std::unordered_map<uint16_t, PerSlabBits>` with `std::vector<char>(SLAB_SIZE)` for `seen`/`active`). No writes to slab-resident nodes during visit bookkeeping. Memory: ~2KB per visited slab (dense) vs ~40 bytes/node (set). Benchmark: 10 traversals of 200-node tree under 10ms. Validation: listree_tests 83/83 (+7 new), Edict 75/75.
- **G104 is now complete.** Immutable Code Object / Activation Frame Split delivered: `makeCodeFrame(...)` creates immutable `.code_frame` binary code objects, `EdictVM::code_ips_` maintains activation-local instruction pointers as a private `std::vector<int>`, recursive freeze now marks binary thunk/code objects read-only, and 📄[WP — Code Object / Activation State Boundary](./Knowledge/WorkProducts/WP-CodeObjectActivationBoundary-2026-05-25/index.md) documents static-shareable vs activation-local state. Validation: frame tests 3/3, VM regression 48/48, intern/root1 20/20. G096 now supplies the session/static-mount resume boundary; deeper static core image versioning remains future work.
- **G092 is now complete.** Cartographer FFI Re-entrancy Metadata delivered: extended `symbolDeclaration()` with 8 capability fields (`thread_safe`, `process_safe`, `reentrant`, `pure`, `side_effects`, `credential_bearing`, `static_shareable_declaration`, `requires_process_local_binding`), Cartographer import classification with conservative heuristics via `annotateImportedNode()`, validation rejecting images missing required capability fields, and two dedicated tests (`WorkerPrimitiveSymbolsCarryCapabilityMetadata`, `WorkerPrimitiveSymbolsHaveValidSideEffects`). StaticDeclarationImageTest 14/14, InternWorkerTest 27/27. Phase 5 (worker runtime enforcement) deferred to G107 process validation.
- **G107 is now complete.** The process-isolated micro-VM intern substrate has been fully implemented and documented across all launch modes:
  - Pre-isolation shared-code smoke (thread worker + frozen/static-immortal base thunk)
  - Static-image/mmap-backed launch-contract smoke (read-only `static_mounts` descriptors)
  - Native forked worker-process smoke (fork + inherited shared base + read-only static mounts)
  - Public async `intern_start!`/`intern_sync!` process-backed worker path (fork backend)
  - Fork/exec worker smoke (independent G103 static declaration image mmap mount)
  - Public async fork/exec static-image worker path through `intern_start!`/`intern_sync!`
  - Static program-entry exec contract (resolve `program_source` from independently mounted image)
  - Static bytecode-entry exec contract (resolve `bytecode_hex` from independently mounted image)
  - **OS-backed borrowed bytecode-slab exec contract** (`bytecode_slab_path` + offset/size/hash → `mmap(PROT_READ)` → `EdictVM::executeBorrowedStaticCode()`)
  - 🔗[WP — G107 Process-Isolated Launch Contract](./Knowledge/WorkProducts/WP_G107_ProcessIsolatedLaunchContract.md) documents the full inherited/rehydrated/blocked handle/capability policy
- The handle/capability policy defines three categories: **Inherited** (frozen read-only Listree, static-immortal slabs — safe via fork COW), **Rehydrated** (context/imports/input via JSON pipe, static_mounts via independent mmap — safe by construction), and **Blocked** (raw fds, provider handles, credentials, VM pointers, activation frames, dlopen handles — never cross coordinator→worker boundary).
- Static declaration image validation now includes tests that reject images claiming `contains_native_handles = "true"` and symbols with `stores_native_handle = "true"`.
- **G110 is complete.** Root1 abandoned recovery has explicit resource recovery, lease expiry, participant heartbeat renewal, and pidfd owner-death lease recovery; the imported Root1 surface exposes logical resource/lease wrappers, `await!` parks through `Root1AwaitScheduler`, and scheduler state is persisted with named sessions. Full kill-mid-op activation-frame resurrection remains future work outside G110.
- **G091 is complete.** Running `worker.edict_drop!` means handle abandonment, not thread preemption; abandoned detached workers unwind naturally and suppress orphan completion publication. Reopen worker cleanup only when published result slabs, durable async job records, or explicit process-worker arena handles exist.
- **G099 is complete.** Validators remain opt-in for blocking `intern_run!`, while async `intern_start!` enforces the minimal task contract before dispatch. Result trust validation is explicit/opt-in rather than automatically mutating sync envelopes; richer schema/auto-merge policy is future work.

**Do NOT**: Do not add new intern-specific VM opcodes, do not expose raw fds as durable Edict state, and do not pass stateful provider/runtime handles into intern worker context/imports. `await!` is now implemented per the G110 public parking contract; continuation-parking `await!` follows the documented envelope/terminal-handle boundary. G096 now persists logical root/scheduler/static-mount state; full kill-mid-op activation-frame serialization and stable cross-version code-object identity remain future work.

## Active Agents
None.

## Knowledge Inventory

### Goals — incomplete priority view (5)
- 🔗[G095 — Edict Cognitive Skill Scaffolds](./Knowledge/Goals/G095-EdictCognitiveSkillScaffolds/index.md) — planned investigation/review/refactor scaffolds.
- 🔗[G097 — Composite Speculation + Logic + FFI Demo](./Knowledge/Goals/G097-CompositeSpeculationLogicFfiDemo/index.md) — planned integrated showcase/demo.
- 🔗[G098 — Architectural Vision Work Product](./Knowledge/Goals/G098-ArchitecturalVisionWorkProduct/index.md) — planned durable work product for the extracted architecture summary.
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictNativeArchitectures/index.md) — deferred speculative reasoning architecture.
- 🔗[G093 — Reference-Scoped ReadOnly Sharing](./Knowledge/Goals/G093-ReferenceScopedReadOnlySharing/index.md) — deferred finer-grained ReadOnly/shadowing refinement.

### WorkProducts — active references (18)
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
- [x] Split module-backed `intern_cancel!` into Edict-level request-cancel/collect composition
- [x] Added explicit module-backed worker drop cleanup primitive and regression coverage
- [x] Split module-backed `intern_run!` / `intern_start!` into prepare/capacity/prepared-start composition with regression coverage
- [x] Added Edict-owned backpressure envelope helper and first importable `root1.edict` participant/poll/mailbox primitive wrappers
- [x] Added module-backed collect-status facts and Edict-owned async sync/cancel public envelopes
- [x] Deleted intern VM opcode enum/dispatch/bootstrap shims and ported public intern tests to module imports
- [x] Added run-status/start-status facts, moved run/start public envelopes into `intern.edict`, removed raw envelope worker words/check-capacity from the imported surface, renamed worker primitive implementation, and split worker runtime / thin C ABI wrappers
- [x] Completed G111 and removed it from the incomplete-priority backlog
- [x] Advanced G091 lifecycle cleanup with retained terminal statuses, explicit drop/abandon semantics, active-count cleanup, and non-retroactive cancellation
- [x] Started G099 first contract-validation slice with `intern.validate_task_contract!` and `intern.validate_status_envelope!`
- [x] Advanced G110 with known-resource abandoned-owner recovery and non-parking module-backed `root1.await!`
- [x] Updated Dashboard, Goal files, Fact/Timeline state, and fresh Handoff Note
- [x] Advanced G104 with private VM instruction-pointer activation state for immutable code frames, fixed speculative nested-frame cleanup, and validated focused VM/intern/root1 slices
- [x] Documented the G104 static-shareable code object vs activation-local VM state boundary and tightened recursive freeze semantics to include binary thunk/code objects
- [x] Added the first G107 pre-isolation worker smoke executing a shared frozen/static-immortal base code thunk
- [x] Added the first G107 static-image/mmap-backed worker launch-contract smoke via read-only `static_mounts`
- [x] Added the first G107 native forked worker-process smoke over inherited frozen/static-immortal shared base code and read-only `static_mounts`
- [x] Added the first G107 public async `intern_start!` / `intern_sync!` process-backed worker path over the forked worker seam
- [x] Added the first G107 fork/exec worker smoke with independently mounted G103 static declaration image
- [x] Added the first G107 public async fork/exec static-image worker path through `intern_start!` / `intern_sync!`
- [x] Added the first G107 static program-entry exec contract resolved from an independently mounted static image
- [x] Added the first G107 static bytecode-entry exec contract resolved from an independently mounted static image
- [x] Added the first G107 OS-backed borrowed bytecode-slab exec contract resolved from an independently mounted static image
- [x] Completed G107 handle/capability policy documentation and validation: added 🔗[WP — G107 Process-Isolated Launch Contract](./Knowledge/WorkProducts/WP_G107_ProcessIsolatedLaunchContract.md) documenting inherited/rehydrated/blocked handle categories, extended static declaration image validation with `contains_native_handles` and `stores_native_handle` rejection tests, and updated Dashboard to reflect G107 completion
- [x] Completed G092 Cartographer FFI Re-entrancy Metadata: extended symbol metadata with 8 capability fields, Cartographer import classification with conservative defaults, validation and test coverage; closed out goal with Dashboard updates
- [x] Completed G104 Immutable Code Object / Activation Frame Split: immutable code objects, activation-local IPs, recursive freeze on binary thunks, boundary documentation; closed out goal with Dashboard updates
- [x] Advanced G108 Cursor-Scoped Traversal Visit Bitmaps: replaced TraversalContext with TraversalVisitState (per-slab bitmaps), updated all call sites, added 5 independent-traversal/cycle-handling tests
- [x] Completed G108: benchmark/sanity-check, all acceptance criteria met, closed out with Dashboard updates, Next Action advanced to G096 at the time
- [x] Completed G096 authoritative mmap session resume: layered static-mount restore acceptance test, file-backed/session persistence validation, WP_G096 durable/transient/deferred boundary, and Dashboard/Timeline reconciliation
- [x] Advanced G106: logical publication layer registry MVP, owner/stale/expiry tests, and HRM goal/Dashboard/Timeline updates
- [x] Completed G106: logical publication layer registry, manifest-file/hash/root metadata validation, WorkProduct, and HRM closeout
- [x] Completed G103 static declaration image MVP and retired it from the open Dashboard backlog.
- [x] Completed post-G106 reconciliation: G103 retired as complete, G078/G110 remaining-work boundaries refreshed, and Dashboard Next Action advanced to G101.
- [x] Completed G101 Direct Edict Tool-Emission Path: added `agentc_direct_action!`, curated launcher global imports, direct/read-file and denied-shell tests, WP_G101, and Dashboard/Timeline closeout.
- [x] Completed G100 Edict Isolation Contract Hardening: added isolated-call parent binding/leak tests, explicit context mutation test, LLM guide examples, WP_G100, Dashboard/Timeline closeout, and recorded unrelated full-edict ReproFFI/Callback blocker.
- [x] Implemented G094.5 (knowledge graph): Listree-backed graph with nodes/edges, 7 VM opcodes, `kgraph` capsule (`create`/`add_node`/`add_edge`/`get_node`/`query`/`nodes`/`edges`), wildcard edge queries, cascade node removal, 7 KG unit tests + 3 Edict-level KG tests. G094 marked COMPLETE.
- [x] Implemented G094.4 (structural diff engine): LCS-based AST comparison with Added/Removed/Modified/Moved detection, `treesitter.diff!` Edict builtin, 6 diff unit tests + 2 Edict-level diff tests, runnable example.
- [x] Implemented G094.1–G094.3 (tree-sitter AST bridge): Edict tree-sitter grammar (`treesitter/edict/grammar.js`), C++ bridge (`tree_sitter_bridge.cpp`), Edict VM builtins (`treesitter.load!`/`treesitter.parse!`/`treesitter.list!`), 15 bridge tests + 7 Edict integration tests. Full `edict_tests` 181/181, `treesitter_tests` 15/15.
- [x] Root-caused and fixed ReproFFI/Callback full-suite blocker: (a) `evalDispatchFFI` allowed parser-mapped functions without `resolution_status` to be invoked (previously silently returned TOS), (b) `op_SPLICE` empty-string auto-resolve via `Cursor::resolve("")` returned the local scope itself, which SPLICE then converted to list mode, destroying all named bindings; fix skips auto-resolve for zero-length strings, (c) default `EdictVM` constructor now creates a non-null cursor root for consistent VM/cursor root sharing, (d) regression test `ReproFFITest.SpliceEmptyStringDoesNotCorruptLocalScope` added. Full `edict_tests` 174/174, listree 83/83, reflect 55/55, cartographer 52/52, cpp_agent 55/55.

- [x] Completed G078/G110 closeout: Edict catalog-driven provider/root rehydration, Root1 design/prototype retirement, Dashboard/Timeline/goal reconciliation, and full baseline validation.
- [x] Completed G091/G099 closeout: intern-worker concurrency MVP and quality-contract MVP retired from the incomplete backlog after focused 39/39 validation.
