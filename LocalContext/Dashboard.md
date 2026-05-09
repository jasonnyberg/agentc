# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-05-09

## Open Goals
### Planned / Active
- 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) — **IN PROGRESS**
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — PLANNED

### Complete (this cycle)
- 🔗[G076 — Generalized Multiline Edict Accumulator](./Knowledge/Goals/G076-GeneralizedMultilineAccumulator/index.md) — **COMPLETE** (2026-05-09)
- 🔗[G073 — Pure VM Lifecycle & Host Thinning](./Knowledge/Goals/G073-PureVMLifecycleHostThinning/index.md) — **COMPLETE** (2026-05-06)
- 🔗[G072 — Direct Slab Restore Without Full Library Re-import](./Knowledge/Goals/G072-DirectSlabRestoreWithoutReimport/index.md) — **COMPLETE** (2026-05-06)

### Blocked
None

## Active Context
- **G074 Streaming Architecture Locked**: We have designed the "Decoupled Ghost Queue" approach. Background LLM network streams will write to standard C++ `std::queue`s (ephemeral, disposable RAM). The Edict VM's main thread will explicitly pull from these queues via `agentc_stream_sync !` to mutate its persistent Listree mailboxes. This guarantees Listree mmap safety and deterministic snapshot resilience.
- **Resilience**: If the VM restarts, background C++ threads die. The VM handles stuck mailboxes using native timeouts.

## Knowledge Inventory
- **Category Indexes**: `Knowledge/Concepts/index.md`; `Knowledge/Facts/index.md`; `Knowledge/Procedures/index.md`

## Handoff Note

**Project**: AgentC
**Current State**: G074 architecture is locked and documented.
**Next Action**: Implement the `StreamManager` class in the C++ runtime to manage thread-safe token queues, then build the `agentc_call_stream` and `agentc_stream_sync` FFI bindings.
**Key Context**: The FFI stream waiter must be entirely stateless and ephemeral. The VM must decide *when* to sync the Listree.

## Session Compliance
- [x] Reviewed Dashboard at session start
- [x] Created/updated Goal files for multi-step architectural work
- [x] Persisted durable knowledge needed for future continuation
- [x] Updated Dashboard with current focus and next action
- [x] Left a handoff note for the next session
