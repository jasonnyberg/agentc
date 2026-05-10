# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-05-10

## Open Goals
### Planned / Active
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — **IN PROGRESS**
- 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) — **IN PROGRESS**
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — PLANNED

### Complete (this cycle)
- 🔗[G077 — Local LLM Demo Request Diagnostics](./Knowledge/Goals/G077-LocalLlmDemoRequestDiagnostics/index.md) — **COMPLETE** (2026-05-10)
- 🔗[G076 — Generalized Multiline Edict Accumulator](./Knowledge/Goals/G076-GeneralizedMultilineAccumulator/index.md) — **COMPLETE** (2026-05-09)
- 🔗[G073 — Pure VM Lifecycle & Host Thinning](./Knowledge/Goals/G073-PureVMLifecycleHostThinning/index.md) — **COMPLETE** (2026-05-06)
- 🔗[G072 — Direct Slab Restore Without Full Library Re-import](./Knowledge/Goals/G072-DirectSlabRestoreWithoutReimport/index.md) — **COMPLETE** (2026-05-06)

### Blocked
None

## Active Context
- **G074 Streaming Architecture Locked**: We have designed the "Decoupled Ghost Queue" approach. Background LLM network streams will write to standard C++ `std::queue`s (ephemeral, disposable RAM). The Edict VM's main thread will explicitly pull from these queues via `agentc_stream_sync !` to mutate its persistent Listree mailboxes. This guarantees Listree mmap safety and deterministic snapshot resilience.
- **Resilience**: If the VM restarts, background C++ threads die. The VM handles stuck mailboxes using native timeouts.
- **Local Runtime Demo Fixed**: `./demo_local_llm.sh` now works against the local OpenAI-compatible server after removing the 30-second SSE timeout, adding outbound request diagnostics, and teaching request normalization to accept `messages[*].content`.
- **New High-Priority Consolidation Track**: Request/config/root/UI-loop semantics are currently split across shell demos, Edict modules, host bootstrap, runtime normalization, and provider adapters. The desired direction is to make Edict the authoritative control plane and reduce the runtime/host to thinner execution and lifecycle layers. See 🔗[WP — Edict-Resident Agent Loop Consolidation Plan](./Knowledge/WorkProducts/WP-EdictResidentAgentLoopConsolidation-2026-05-10/index.md).
- **First Provider-Contract Slice Landed**: `agentc_provider_contracts.edict` now carries contrasting `local` and `google` provider semantics in Edict data, `agentc_agent_root.edict` builds canonical turn requests from `runtime.provider_contract`, embedded-VM bootstrap imports the new module, and targeted regression coverage across root-building, embedded turns, restore, and session persistence is passing.

## Knowledge Inventory
- **Category Indexes**: `Knowledge/Concepts/index.md`; `Knowledge/Facts/index.md`; `Knowledge/Procedures/index.md`

## Handoff Note

**Project**: AgentC
**Current State**: G074 architecture is locked and documented. Local OpenAI-compatible demo diagnostics are complete and verified. G078 has advanced from planning into the first working Edict provider-contract slice: the VM now carries provider contracts in runtime config, the root module builds canonical requests from that contract, and the embedded VM path has passing targeted coverage again.
**Next Action**: Continue G078 by removing the temporary C++ duplication that enriches `runtime.provider_contract`, then start lifting more runtime/bootstrap and UX-loop authority out of `main.cpp` and into Edict-native control-plane code.
**Key Context**: The FFI stream waiter must be entirely stateless and ephemeral. The VM must decide *when* to sync the Listree. The local demo now logs full outbound OpenAI-compatible requests for future debugging. The new provider-contract slice works for contrasting `local` and `google` semantics, but C++ still mirrors the contract during bootstrap/rehydration as a temporary bridge.

## Session Compliance
- [x] Reviewed Dashboard at session start
- [x] Created/updated Goal files for multi-step architectural work
- [x] Persisted durable knowledge needed for future continuation
- [x] Updated Dashboard with current focus and next action
- [x] Left a handoff note for the next session
