# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-04-30

## Current Focus
- **Active Goals**:
  - **G068** — Client/Agent Split with Embedded Persistent Edict VM — **IN PROGRESS** 🔗[index](./Knowledge/Goals/G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
  - **G069** — Remove LMDB Dependencies — **COMPLETE** 🔗[index](./Knowledge/Goals/G069-RemoveLMDBDependencies/index.md)
- **Current Task**: Begin the runtime extraction in code: stand up `libagent_runtime.so`, keep mmap-backed Listree persistence as the durable substrate, and start moving the host toward the new runtime boundary.
- **Immediate Next Action**: Refactor the current host/front-end to consume `libagent_runtime` through the new runtime core, then add the first Edict-side `agentc` wrapper/import path over the C ABI.

## Active Context
- Proven working path: `cpp-agent` can now serve a persistent socket session and complete a Gemini-backed interaction end-to-end when the client keeps the connection open.
- Architecture decision retained: preserve **Client ↔ Host** separation for reconnectable UI sessions, but avoid **Host ↔ Edict VM** process separation.
- Architecture refinement: the canonical agent loop should move into **Edict**; the host should embed the interpreter/VM and expose interchangeable front-ends (shell, socket, pipe, file) rather than owning a hardcoded outer C++ orchestration loop.
- Module direction: `agentc` should become an importable Edict capability/module backed by a reusable native runtime library that handles provider selection, credentials, HTTP transport, and response normalization.
- Runtime implementation has started: `cpp-agent/include/agentc_runtime/agentc_runtime.h`, runtime core/c_api scaffolding, built-in provider registration, and a shared `libagent_runtime.so` target now exist and build successfully.
- Validation so far: `agent_runtime` and `cpp-agent` build successfully, and a Python `ctypes` smoke test successfully created a runtime handle and received a normalized JSON error response from `libagent_runtime.so`.
- Safety boundary: model output should be consumed as **normalized JSON → Edict data first**; unrestricted model-driven FFI/native access remains an explicit off-by-default switch.
- Persistence decision: use **memory-mapped slab persistence** as the canonical durable state mechanism and rehydrate transient runtime artifacts such as FFI handles, sockets, provider clients, and parser state on restore.
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
The project is evolving toward an Edict-native agent runtime: keep the reconnectable Client/Host boundary and embedded VM, but move the canonical agent loop into Edict and expose LLM access as an importable `agentc` module backed by a reusable native runtime library. The first implementation slice is now landed: `libagent_runtime.so` builds from the new `cpp-agent/runtime/` scaffolding, the public C ABI header exists at `cpp-agent/include/agentc_runtime/agentc_runtime.h`, runtime JSON request/config normalization is implemented, and a `ctypes` smoke test verified runtime creation plus normalized JSON error return. The exact next action is to refactor the current host/front-end to consume `libagent_runtime` through the new runtime core, then add the first Edict-side `agentc` wrapper/import path. Key context: keep Client/Host separation, keep Host+VM embedded, keep mmap-backed Listree/slab persistence as the durable substrate, treat provider/runtime handles as transient rehydrated state, and keep unrestricted model-driven FFI/native access disabled by default. Do NOT preserve the outer C++ agent loop as the long-term owner of policy/orchestration; it is now a transitional prototype, not the target architecture.

## Session Compliance
- [x] Reviewed Dashboard at session start
- [x] Created/updated Goal files for multi-step architectural work
- [x] Persisted durable knowledge needed for future continuation
- [x] Updated Dashboard with current focus and next action
- [x] Updated project history / timeline
- [x] Left a handoff note for the next session
- [x] Kept memory to the minimum needed for continuation
