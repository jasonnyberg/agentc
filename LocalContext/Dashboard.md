# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-04-30

## Current Focus
- **Active Goals**:
  - **G068** — Client/Agent Split with Embedded Persistent Edict VM — **IN PROGRESS** 🔗[index](./Knowledge/Goals/G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
  - **G069** — Remove LMDB Dependencies — **COMPLETE** 🔗[index](./Knowledge/Goals/G069-RemoveLMDBDependencies/index.md)
- **Current Task**: Continue from the new config-file-driven runtime selection path into the transient rehydration boundary work around embedded VM restore.
- **Immediate Next Action**: Define and implement the transient rehydration boundary for runtime handles/imports around embedded VM restore while continuing to move turn/state transition logic inward.

## Active Context
- Proven working path: `cpp-agent` can now serve a persistent socket session and complete a Gemini-backed interaction end-to-end when the client keeps the connection open.
- Architecture decision retained: preserve **Client ↔ Host** separation for reconnectable UI sessions, but avoid **Host ↔ Edict VM** process separation.
- Architecture refinement: the canonical agent loop should move into **Edict**; the host should embed the interpreter/VM and expose interchangeable front-ends (shell, socket, pipe, file) rather than owning a hardcoded outer C++ orchestration loop.
- Module direction: `agentc` should become an importable Edict capability/module backed by a reusable native runtime library that handles provider selection, credentials, HTTP transport, and response normalization.
- Runtime implementation is now past scaffolding: `libagent_runtime.so` builds, `cpp-agent/main.cpp` has been refactored into a thinner runtime-backed socket host, `shutdown-agent` now stops the daemon cleanly, `reset-session` clears in-memory and persisted conversation state, and host config can be supplied through `--config` or provider/model flags.
- Edict bridge implementation now exists: `cpp-agent/edict/modules/agentc.edict` wraps the runtime C ABI through Cartographer imports plus `extensions/libagentc_extensions.so`, and `cpp-agent/demo/demo_agentc_runtime_edict.sh` demonstrates the data-first Edict path end-to-end.
- Live Edict↔LLM verification is now complete: Edict directly imported `libagent_runtime.so`, created a runtime handle, issued a live Google/Gemini request, and received a normalized assistant response (`"Hello world"`) back through the `agentc_call` wrapper path.
- The inverted-loop POC ladder is now in place:
  - `cpp-agent/edict/modules/agentc_hello_loop.edict` proves Edict can own a single-step LLM turn.
  - `cpp-agent/edict/modules/agentc_stateful_loop.edict` proves Edict can own explicit multi-turn conversation history in native `messages` data.
  - `cpp-agent/edict/modules/agentc_agent_root.edict` proves Edict can return a canonical native root object with `conversation`, `memory`, `policy`, `runtime`, and `loop` sections.
- Live demos now exist for each rung of that ladder:
  - `cpp-agent/demo/demo_inverted_hello_loop.sh`
  - `cpp-agent/demo/demo_inverted_stateful_loop.sh`
  - `cpp-agent/demo/demo_inverted_agent_root.sh`
- Automated validation is now in place: `cpp_agent_tests` covers the raw runtime C ABI, the Edict runtime wrapper, hello-loop helper, stateful conversation-history loop, canonical agent-root helper, `AgentRootStateTest`, and the `SessionStateStore` Listree-backed persistence path; it passes under direct execution and `ctest -R cpp_agent_tests`.
- Persistence hook-up has advanced from transcript snapshots to canonical root persistence: `cpp-agent/runtime/persistence/agent_root_state.*` defines host-side canonical-root creation/normalization/update helpers, `cpp-agent/main.cpp` restores and persists the canonical agent-root object through `SessionStateStore`, and host startup/reset reconfigures `libagent_runtime` from persisted `root.runtime` metadata.
- The host is now also lifecycle-coupled to an embedded VM/root-anchor restore path: `SessionStateStore` exposes `loadRoot(...)` / `saveRoot(...)`, `cpp-agent/main.cpp` constructs an `EdictVM` around the restored anchored root, resets/replaces that VM root as state changes, and persists the VM-owned root anchor after turns.
- New automated coverage exists for that slice: `EmbeddedVmRootRestoreTest` proves a canonical persisted root can be restored as a native anchored Listree root and used to construct a fresh `EdictVM` with the expected restored state.
- The persistence demo path was also cleaned up: `cpp-agent/demo/demo_runtime_persistence.sh` now runs reliably under zsh and no longer collides with the ambient `HOST` environment variable while demonstrating `Restored persisted agent root ...` on restart.
- A follow-up default-model switch attempt showed that `models/gemini-3.1-flash` is not currently accepted by the active Google `v1beta:generateContent` path, so the sample/operator defaults have been re-centered on the last known-good `gemini-2.5-flash` model instead of leaving the system pointed at a broken identifier.
- Provider/model selection is now operator-configured rather than source-configured: a project-root `agentc-config.json` file exists, `cpp-agent/main.cpp` honors `AGENTC_CONFIG` and auto-detects `./agentc-config.json`, `cpp-agent/edict/modules/agentc.edict` now exposes file/path-based config helpers (including an Edict-native `read_text` + `from_json` path), and the Edict/runtime demos load runtime config from that file instead of embedding model literals.
- The config-driven flow has been live-validated: `demo_agentc_runtime_edict.sh` now resolves runtime config from `agentc-config.json` without recompilation, and `demo_runtime_persistence.sh` succeeded again using `gemini-2.5-flash`, including restart continuity with `Restored persisted agent root ...` on the second host run.
- Physical source migration has started in earnest: the active `agent_runtime` target now builds from `cpp-agent/runtime/common/{credentials,http_client,sse_parser}.*`, `cpp-agent/runtime/core/provider_registry.*`, `cpp-agent/runtime/providers/{google,openai}/...`, and `cpp-agent/runtime/persistence/session_state_store.*` instead of the older top-level source locations.
- Transitional compatibility shims now exist for older top-level runtime-support paths (`api_registry.*`, `credentials.*`, `http_client.*`, `sse_parser.*`, `providers/google.*`, `providers/openai.*`) so migration can continue without breaking legacy includes immediately.
- Safety boundary: model output should be consumed as **normalized JSON → Edict data first**; unrestricted model-driven FFI/native access remains an explicit off-by-default switch.
- Persistence decision: use **memory-mapped/Listree/slab-backed persistence** as the canonical durable state mechanism and rehydrate transient runtime artifacts such as FFI handles, sockets, provider clients, and parser state on restore.
- LMDB cleanup is complete: the root build flag and `LmdbArenaStore` are gone, LMDB-only test/demo branches are removed, and active code/build paths no longer reference LMDB.

## Status
- G057 (Pi + AgentC IPC Bridge) - **COMPLETE**
- G058 (Read-Only Listree Branches) - **COMPLETE**
- G059 (Listree JSON Round-Trip) - **COMPLETE**
- G060 (Pi Frontend Integration) - **COMPLETE**
- G061 (AgentC Stability and Hardening) - **COMPLETE**
- G063 (Native C++ Agent Core) - **COMPLETE** 🔗[index](./Knowledge/Goals/G063-NativeCppAgentCore/index.md)
- G066 (Socket-Enabled Agent) - **COMPLETE** 🔗[index](./Knowledge/Goals/G066-SocketEnabledAgent/index.md)
- G067 (Atomic AgentC Agent) - **COMPLETE / EVOLVED** 🔗[index](./Knowledge/Goals/G067-AtomicAgentcAgent/index.md)
- G068 (Client/Agent Split with Embedded Persistent Edict VM) - **IN PROGRESS** 🔗[index](./Knowledge/Goals/G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
- G069 (Remove LMDB Dependencies) - **COMPLETE** 🔗[index](./Knowledge/Goals/G069-RemoveLMDBDependencies/index.md)

## Knowledge Inventory
- **Goals**: G016-LmdbOptionalBuild; G017-EdictScriptMode; G018-FfiLtvPassthrough; G019-SlabIdLtvUnification; G020-EarlyTypeBinding; G021-RemoveModuleName; G040_LMDB_Persistent_Arena_Integration; G041_Persistent_Slab_Image_Persistence; G042_Persistent_VM_Root_State_And_Restore_Validation; G043-LmdbPickling; G044_JSON_Module_Cache; G045-LanguageEnhancementReview; G046-ContinuationBasedSpeculation; G047-NativeRelationalSyntax; G048-LibraryBackedLogicCapability; G049-EdictVMMultithreading; G050-ThreadSpawnJoinMixedRunStability; G051-CursorVisitedReadOnlyBoundary; G052-RuntimeBoundaryHardening; G053-SharedRootFineGrainedMultithreading; G054-SdlImportDemo; G055-NativeSdlPoc; G056-ExtensionsStdlib; G057-PiAgentcIpcBridge; G058-ReadOnlyListreeBranches; G059-ListreeToJsonRoundTrip; G060-PiFrontendIntegration; G061-AgentCStabilityAndHardening; G062-LogicEngineBootstrapping; G063-NativeCppAgentCore; G066-SocketEnabledAgent; G067-AtomicAgentcAgent; G068-ClientAgentSplitEmbeddedVmPersistence; G069-RemoveLMDBDependencies.
- **Facts**: 🔗[ListreeTraversalCycleDetection](./Knowledge/Facts/ListreeTraversalCycleDetection.md)
- **Contracts**: 🔗[AgentcEvalContract](./Knowledge/Contracts/AgentcEvalContract.md); 🔗[AgentcRuntimeCAbi](./Knowledge/Contracts/AgentcRuntimeCAbi.md); 🔗[AgentcRuntimeJsonContract](./Knowledge/Contracts/AgentcRuntimeJsonContract.md)
- **Procedures**: 🔗[AgentC_IPC_Bridge_Ops](./Knowledge/Procedures/AgentC_IPC_Bridge_Ops.md); 🔗[AgentC_Socket_Ops](./Knowledge/Procedures/AgentC_Socket_Ops.md)
- **Prompts**: 🔗[SystemPrompt_AgentC](./Knowledge/Prompts/SystemPrompt_AgentC.md)
- **WorkProducts**: 🔗[AgentCLanguageEnhancements-2026-03-22](./Knowledge/WorkProducts/AgentCLanguageEnhancements-2026-03-22.md); 🔗[ContinuationBasedSpeculationPlan-2026-03-22](./Knowledge/WorkProducts/ContinuationBasedSpeculationPlan-2026-03-22.md); 🔗[edict_language_reference](./Knowledge/WorkProducts/edict_language_reference.md); 🔗[EdictVMMultithreadingPlan-2026-03-23](./Knowledge/WorkProducts/EdictVMMultithreadingPlan-2026-03-23.md); 🔗[LogicCapabilityMigrationPlan-2026-03-23](./Knowledge/WorkProducts/LogicCapabilityMigrationPlan-2026-03-23.md); 🔗[NativeRelationalSyntaxPlan-2026-03-22](./Knowledge/WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md); 🔗[WP_NativeCppAgentCore_Audit](./Knowledge/WorkProducts/WP_NativeCppAgentCore_Audit.md); 🔗[WP_EmbeddedPersistentAgentArchitecture](./Knowledge/WorkProducts/WP_EmbeddedPersistentAgentArchitecture.md); 🔗[WP_EdictNativeAgentModuleArchitecture](./Knowledge/WorkProducts/WP_EdictNativeAgentModuleArchitecture.md); 🔗[WP_CppAgentRuntimeFileStructure](./Knowledge/WorkProducts/WP_CppAgentRuntimeFileStructure.md); 🔗[WP_LMDB_SurfaceArea_Audit_2026-04-30](./Knowledge/WorkProducts/WP_LMDB_SurfaceArea_Audit_2026-04-30.md)
- **Category Indexes**: `Knowledge/Concepts/index.md`; `Knowledge/Facts/index.md`; `Knowledge/Procedures/index.md`

## Active Agents
- None.

## Project History
- 🔗[Timeline](./Timeline.md)

## Handoff Note
The project is evolving toward an Edict-native agent runtime: keep the reconnectable Client/Host boundary and embedded VM, but move the canonical agent loop into Edict and expose LLM access as an importable `agentc` module backed by a reusable native runtime library. The runtime bridge is now working end-to-end, has automated coverage, includes a deeper persistence hook, has been live-verified against Gemini from inside Edict itself, and now has hello-world, multi-turn history, canonical agent-root, host-root-persistence, and embedded-VM/root-anchor slices: `libagent_runtime.so` builds from the new runtime scaffolding, `cpp-agent/main.cpp` is a thinner runtime-backed socket host with clean `shutdown-agent` and `reset-session` behavior, `cpp-agent/edict/modules/agentc.edict` proves the Edict-side import/wrapper path, `cpp_agent_tests` validates the raw C ABI + Edict wrapper + root helpers + persistence path, `cpp-agent/runtime/persistence/session_state_store.*` now exposes both JSON and native root-anchor restore/save helpers, `cpp-agent/runtime/persistence/agent_root_state.*` shapes the canonical root object, `cpp-agent/main.cpp` now constructs an embedded `EdictVM` around the restored anchored root, `cpp-agent/edict/modules/agentc_hello_loop.edict` proves Edict can own the single-step LLM turn, `cpp-agent/edict/modules/agentc_stateful_loop.edict` proves Edict can carry explicit native multi-turn conversation history, and `cpp-agent/edict/modules/agentc_agent_root.edict` proves Edict can return a canonical native root object with `conversation`, `memory`, `policy`, `runtime`, and `loop` sections. The exact next action is to define and implement the transient rehydration boundary for runtime handles/imports around this embedded VM restore path while continuing to shrink the remaining host-owned JSON mutation logic. Key context: keep Client/Host separation, keep Host+VM embedded, keep mmap/Listree/slab persistence as the durable substrate, treat provider/runtime handles as transient rehydrated state, and keep unrestricted model-driven FFI/native access disabled by default. Do NOT reintroduce host-owned orchestration as the long-term architecture; the remaining outer-host logic is transitional and should keep shrinking.

## Session Compliance
- [x] Reviewed Dashboard at session start
- [x] Created/updated Goal files for multi-step architectural work
- [x] Persisted durable knowledge needed for future continuation
- [x] Updated Dashboard with current focus and next action
- [x] Updated project history / timeline
- [x] Left a handoff note for the next session
- [x] Kept memory to the minimum needed for continuation
