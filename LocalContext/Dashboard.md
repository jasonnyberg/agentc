# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-05-06

## Open Goals
### Active
- 🔗[G073 — Pure VM Lifecycle & Host Thinning](./Knowledge/Goals/G073-PureVMLifecycleHostThinning/index.md) — IN PROGRESS

### Planned / Investigation
- 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) — PLANNED
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — PLANNED

### Complete (this cycle)
- 🔗[G072 — Direct Slab Restore Without Full Library Re-import](./Knowledge/Goals/G072-DirectSlabRestoreWithoutReimport/index.md) — **COMPLETE** (2026-05-06)
- 🔗[G071 — Session-Scoped Allocator Image Persistence](./Knowledge/Goals/G071-SessionScopedAllocatorImagePersistence/index.md) — **COMPLETE** (2026-05-04)
- 🔗[G068 — Client/Agent Split with Embedded Persistent Edict VM](./Knowledge/Goals/G068-ClientAgentSplitEmbeddedVmPersistence/index.md) — **COMPLETE** (2026-05-06)

### Blocked
None

## Active Context
- **G073 Progress**: The C++ host has been drastically thinned! `cpp-agent/main.cpp` now only listens for socket I/O and calls `run_vm_agent_turn_native`. All legacy C++ cognitive loop overrides (`run_vm_agent_root_turn_via_imported_runtime`, manual JSON shaping scripts) are deleted. Turns run completely natively via `agentc_agent_root_turn !` directly on the memory-mapped stack!
- **G068, G071, and G072 are fully complete!** The runtime now executes directly from `mmap` backing files. The C++ host has been drastically thinned, the embedded `EdictVM` acts as the engine, and transient library states repatch automatically using robust dynamic hash-checking.
- **Persistence Foundation**: The C++ agent runtime memory persistence layer is now 100% pure native mmap slabs (`Listree`). Zero JSON serialization layers exist on the persist/restore path.
- **Library Safety**: Cartographer modules safely restore over `mmap` because `validateLibraryFreshness()` dynamically validates `fnv1a64` checksums against `.so` binaries, performing a safe cold-reset via `StaleLibraryException` on any memory layout drift.
- **Edict Ownership**: The cognitive turn execution, FFI bridging, and multi-turn state (`conversation`, `memory`, `policy`) are all owned internally by Edict scripts (`agentc_stateful_loop.edict`, etc.).

## Knowledge Inventory
- **Category Indexes**: `Knowledge/Concepts/index.md`; `Knowledge/Facts/index.md`; `Knowledge/Procedures/index.md`

## Handoff Note

**Project**: AgentC — an Edict-native agent runtime with a mmap/Listree-backed persistence substrate. The `EdictVM` is the embedded sole live owner of session state.
**Current State**: Host C++ orchestration overrides are fully purged (G073 progress). `run_vm_agent_turn_native` now directly pushes string input to the Edict `agentc_agent_root_turn !` function.
**Next Action**: Finish G073 by moving the tree initialization logic from `rehydrate_vm_runtime_state` into a native Edict bootstrap script rather than manually mutating `Listree` memory in C++.
**Key Context**:
- Legacy JSON rematerialization is fully purged. Rely entirely on native `ListreeValue` state mutation.
- Make sure not to overwrite `root.runtime` using `@runtime` internally inside Edict natively without saving the handle, as it corrupts the original configuration dictionary.
**Do NOT**: Reintroduce C++ orchestration for cognitive loops. Keep business logic and state updates entirely inside Edict.

## Session Compliance
- [x] Reviewed Dashboard at session start
- [x] Created/updated Goal files for multi-step architectural work
- [x] Persisted durable knowledge needed for future continuation
- [x] Updated Dashboard with current focus and next action
- [x] Left a handoff note for the next session
