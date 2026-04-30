# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-04-30

## Current Focus
- **Active Goals**:
  - **G068** — Client/Agent Split with Embedded Persistent Edict VM — **PLANNED** 🔗[index](./Knowledge/Goals/G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
  - **G069** — Remove LMDB Dependencies — **COMPLETE** 🔗[index](./Knowledge/Goals/G069-RemoveLMDBDependencies/index.md)
- **Current Task**: Carry the slab-only persistence direction into G068 implementation planning for the reconnectable embedded-agent runtime.
- **Immediate Next Action**: Define embedded-agent startup/shutdown checkpoint semantics and VM restore hooks for the background `cpp-agent` process.

## Active Context
- Proven working path: `cpp-agent` can now serve a persistent socket session and complete a Gemini-backed interaction end-to-end when the client keeps the connection open.
- Architecture decision: preserve **Client ↔ Agent** separation for reconnectable UI sessions, but avoid **Agent ↔ Edict VM** process separation.
- Persistence decision: move toward **memory-mapped slab persistence** as the canonical durable state mechanism.
- Runtime boundary: persist logical VM state and rehydrate transient runtime artifacts such as FFI handles, sockets, and active HTTP/parser state.
- New cleanup requirement: LMDB should be removed from the supported architecture and stripped from active dependencies/references in the codebase.
- Audit completed: LMDB surface area is concentrated in `CMakeLists.txt`, `core/alloc.h`, `core/alloc.cpp`, `tests/alloc_tests.cpp`, `demo/demo_arena_metadata_persistence.cpp`, `README.md`, and a bounded set of LocalContext knowledge files. See 🔗[WP_LMDB_SurfaceArea_Audit_2026-04-30](./Knowledge/WorkProducts/WP_LMDB_SurfaceArea_Audit_2026-04-30.md).
- Cleanup slice completed: `README.md` and key LocalContext persistence knowledge now mark LMDB as legacy/superseded, and LMDB-only test/demo branches have been removed while focused file/memory-backed persistence tests still pass.
- Final LMDB cleanup completed: `AGENTC_WITH_LMDB` has been removed from `CMakeLists.txt`, `LmdbArenaStore` has been deleted from `core/alloc.h` / `core/alloc.cpp`, the active code/build surface contains no LMDB references, and focused rebuild/test validation passes.

## Status
- G057 (Pi + AgentC IPC Bridge) - **COMPLETE**
- G058 (Read-Only Listree Branches) - **COMPLETE**
- G059 (Listree JSON Round-Trip) - **COMPLETE**
- G060 (Pi Frontend Integration) - **COMPLETE**
- G061 (AgentC Stability and Hardening) - **COMPLETE**
- G063 (Native C++ Agent Core) - **COMPLETE** 🔗[index](./Knowledge/Goals/G063-NativeCppAgentCore/index.md)
- G066 (Socket-Enabled Agent) - **COMPLETE** 🔗[index](./Knowledge/Goals/G066-SocketEnabledAgent/index.md)
- G067 (Atomic AgentC Agent) - **COMPLETE / EVOLVED** 🔗[index](./Knowledge/Goals/G067-AtomicAgentcAgent/index.md)
- G068 (Client/Agent Split with Embedded Persistent Edict VM) - **PLANNED** 🔗[index](./Knowledge/Goals/G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
- G069 (Remove LMDB Dependencies) - **COMPLETE** 🔗[index](./Knowledge/Goals/G069-RemoveLMDBDependencies/index.md)

## Knowledge Inventory
- **Goals**: G016-LmdbOptionalBuild; G017-EdictScriptMode; G018-FfiLtvPassthrough; G019-SlabIdLtvUnification; G020-EarlyTypeBinding; G021-RemoveModuleName; G040_LMDB_Persistent_Arena_Integration; G041_Persistent_Slab_Image_Persistence; G042_Persistent_VM_Root_State_And_Restore_Validation; G043-LmdbPickling; G044_JSON_Module_Cache; G045-LanguageEnhancementReview; G046-ContinuationBasedSpeculation; G047-NativeRelationalSyntax; G048-LibraryBackedLogicCapability; G049-EdictVMMultithreading; G050-ThreadSpawnJoinMixedRunStability; G051-CursorVisitedReadOnlyBoundary; G052-RuntimeBoundaryHardening; G053-SharedRootFineGrainedMultithreading; G054-SdlImportDemo; G055-NativeSdlPoc; G056-ExtensionsStdlib; G057-PiAgentcIpcBridge; G058-ReadOnlyListreeBranches; G059-ListreeToJsonRoundTrip; G060-PiFrontendIntegration; G061-AgentCStabilityAndHardening; G062-LogicEngineBootstrapping; G063-NativeCppAgentCore; G066-SocketEnabledAgent; G067-AtomicAgentcAgent; G068-ClientAgentSplitEmbeddedVmPersistence; G069-RemoveLMDBDependencies.
- **Facts**: 🔗[ListreeTraversalCycleDetection](./Knowledge/Facts/ListreeTraversalCycleDetection.md)
- **Contracts**: 🔗[AgentcEvalContract](./Knowledge/Contracts/AgentcEvalContract.md)
- **Procedures**: 🔗[AgentC_IPC_Bridge_Ops](./Knowledge/Procedures/AgentC_IPC_Bridge_Ops.md); 🔗[AgentC_Socket_Ops](./Knowledge/Procedures/AgentC_Socket_Ops.md)
- **Prompts**: 🔗[SystemPrompt_AgentC](./Knowledge/Prompts/SystemPrompt_AgentC.md)
- **WorkProducts**: 🔗[AgentCLanguageEnhancements-2026-03-22](./Knowledge/WorkProducts/AgentCLanguageEnhancements-2026-03-22.md); 🔗[ContinuationBasedSpeculationPlan-2026-03-22](./Knowledge/WorkProducts/ContinuationBasedSpeculationPlan-2026-03-22.md); 🔗[edict_language_reference](./Knowledge/WorkProducts/edict_language_reference.md); 🔗[EdictVMMultithreadingPlan-2026-03-23](./Knowledge/WorkProducts/EdictVMMultithreadingPlan-2026-03-23.md); 🔗[LogicCapabilityMigrationPlan-2026-03-23](./Knowledge/WorkProducts/LogicCapabilityMigrationPlan-2026-03-23.md); 🔗[NativeRelationalSyntaxPlan-2026-03-22](./Knowledge/WorkProducts/NativeRelationalSyntaxPlan-2026-03-22.md); 🔗[WP_NativeCppAgentCore_Audit](./Knowledge/WorkProducts/WP_NativeCppAgentCore_Audit.md); 🔗[WP_EmbeddedPersistentAgentArchitecture](./Knowledge/WorkProducts/WP_EmbeddedPersistentAgentArchitecture.md); 🔗[WP_LMDB_SurfaceArea_Audit_2026-04-30](./Knowledge/WorkProducts/WP_LMDB_SurfaceArea_Audit_2026-04-30.md)
- **Category Indexes**: `Knowledge/Concepts/index.md`; `Knowledge/Facts/index.md`; `Knowledge/Procedures/index.md`

## Active Agents
- None.

## Project History
- 🔗[Timeline](./Timeline.md)

## Handoff Note
The project has pivoted to a clearer runtime architecture: a long-lived background `cpp-agent` process owns the embedded Edict VM and serves reconnectable clients over a Unix socket, while durable state is intended to come from mmap slab persistence rather than a separate VM daemon or LMDB-backed storage path. LMDB cleanup is now complete in the active codebase: the root build flag is gone, `LmdbArenaStore` is removed, LMDB-only tests/demos are removed, and active code/build paths no longer reference LMDB. The next step is to use this simplified slab-only persistence baseline to design embedded-agent checkpoint/restore semantics under G068.

## Session Compliance
- [x] Reviewed Dashboard at session start
- [x] Created/updated Goal files for multi-step architectural work
- [x] Persisted durable knowledge needed for future continuation
- [x] Updated Dashboard with current focus and next action
- [x] Updated project history / timeline
- [x] Left a handoff note for the next session
- [x] Kept memory to the minimum needed for continuation
