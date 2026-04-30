# Goal: G068 - Client/Agent Split with Embedded Persistent Edict VM

## Goal
Establish the next-stage AgentC runtime architecture with a reconnectable client/agent boundary, an in-process embedded Edict VM, and memory-mapped slab persistence for fast restartable durable state.

## Status
**PLANNED**

## Rationale
Recent work proved the value of keeping the interactive client separate from the long-lived agent process: the socket client can disconnect, reconnect, and be replaced without tearing down the active agent session. At the same time, further reflection clarified that splitting the AgentC agent from the Edict VM would introduce unnecessary IPC, marshaling, and lifecycle complexity.

The intended architecture is therefore:
- keep **Client ↔ Agent** as a process boundary,
- keep **Agent + Edict VM** in the same process,
- use **memory-mapped slab persistence** so VM state can be restored quickly,
- treat some runtime artifacts (notably binary FFI handles and other OS-owned resources) as **rehydrated transient state**, not durable serialized state.

This preserves low-latency in-process VM integration while still giving users a durable background agent that supports detach/reattach and later multiplexing.

## Architectural Decision

### Chosen Split
```text
[Client / TUI / CLI]
      ⇅  UDS / stream I/O
[AgentC Agent Process]
      ├─ agent loop
      ├─ provider integration
      ├─ tool dispatch
      ├─ session manager
      ├─ embedded Edict VM
      └─ mmap slab persistence / restore
```

### Explicit Non-Goal
Do **not** split the AgentC agent layer from the Edict VM into separate long-lived processes unless later requirements demand isolation or remote hosting.

## Roles and Communication Boundaries

### Client Layer
**Role**
- user-facing TUI/CLI attachment,
- send prompts and control commands,
- receive streamed or buffered replies,
- disconnect and reconnect safely.

**Communication**
- Unix domain socket (`/tmp/agentc.sock`) or equivalent stream-based transport.

### Agent Layer
**Role**
- long-lived background process,
- owns socket accept loop and session handling,
- runs the agent loop,
- calls Gemini/OpenAI providers,
- owns the embedded Edict VM instance,
- checkpoints/restores VM state.

**Communication**
- upward: stream I/O over Unix domain socket,
- outward: HTTPS/JSON(/SSE where appropriate) to providers,
- inward: direct in-process C++ calls to the embedded Edict VM.

### Embedded Edict VM
**Role**
- durable cognitive state,
- goals, facts, conversation memory, working state,
- tool definitions and execution substrate,
- restorable state image backed by slab persistence.

**Communication**
- no agent/VM IPC boundary,
- direct in-process API boundary,
- persistence through mmap-backed slab image and restore hooks.

## Durable vs Transient State Boundary

### Durable State (persist in slab)
- Listree/object graph state,
- VM roots and anchors,
- agent memory/goals/facts/conversation state worth preserving,
- tool registry metadata and logical configuration,
- resumable workflow/task state.

### Transient Rehydrated State (rebuild on restore)
- FFI dynamic-library handles,
- function pointers and imported runtime binding artifacts,
- open sockets,
- active HTTP streams,
- thread-local runtime state,
- parser buffers / in-flight request state.

## Lifecycle Semantics
- `exit` → disconnect current client session only.
- `shutdown-agent` → checkpoint slab state, terminate the long-lived agent process.
- agent startup → restore VM state from slab when available, then begin accepting clients.
- client restart → reconnect to the same background agent without requiring VM teardown.

## Dependencies / Related Goals
- Builds on prior persistence groundwork:
  - 🔗[G041 Persistent Slab Image Persistence](../G041_Persistent_Slab_Image_Persistence/index.md)
  - 🔗[G042 Persistent VM Root State and Restore Validation](../G042_Persistent_VM_Root_State_And_Restore_Validation/index.md)
- Aligns with interactive runtime work:
  - 🔗[G063 Native C++ Agent Core](../G063-NativeCppAgentCore/index.md)
  - 🔗[G066 Socket-Enabled Agent](../G066-SocketEnabledAgent/index.md)
  - 🔗[G067 Atomic AgentC Agent](../G067-AtomicAgentcAgent/index.md)
- Depends on build/persistence simplification work:
  - 🔗[G069 Remove LMDB Dependencies](../G069-RemoveLMDBDependencies/index.md)

## Acceptance Criteria
- [ ] The architecture is documented as **Client/Agent split, Agent+VM embedded** with no agent/VM IPC dependency.
- [ ] The long-lived agent process can survive client disconnects and accept reconnects over the socket interface.
- [ ] VM persistence design explicitly uses a memory-mapped slab image rather than LMDB as the primary restart mechanism.
- [ ] Durable vs transient runtime state is documented, including explicit treatment of FFI artifacts as rehydrated state.
- [ ] Lifecycle commands are defined for disconnect vs full agent shutdown.
- [ ] LMDB is treated as legacy history, not an active dependency of the target architecture.
- [ ] A concrete demo plan exists for: start agent → interact over socket → disconnect/reconnect → shutdown agent → restore from persisted slab.

## Implementation Plan

### Phase 1 - Architecture and Protocol Consolidation
- [ ] Capture the chosen architecture in a work product.
- [ ] Define client protocol semantics for `exit`, `shutdown-agent`, and reconnect behavior.
- [ ] Decide how multiplexing will be staged: single-controller first, multi-client later.

### Phase 2 - mmap Slab Persistence Design
- [ ] Define the slab file layout and restore contract for the embedded VM.
- [ ] Reconcile existing persistence work (G041/G042) with the agent runtime startup/shutdown path.
- [ ] Remove LMDB as an active design dependency from the supported persistence path.
- [ ] Define what metadata must be persisted for rehydrating FFI imports safely.

### Phase 3 - Embedded VM Lifecycle Hooks
- [ ] Add agent-managed checkpoint/save hooks.
- [ ] Add startup restore hooks.
- [ ] Ensure clean shutdown checkpoints are possible without introducing an Agent↔VM IPC boundary.

### Phase 4 - Client Reattach and Session Management
- [ ] Preserve the background agent process across client disconnects.
- [ ] Support reconnect to the active agent instance.
- [ ] Add safe write/disconnect handling and prepare for multiplexing.

### Phase 5 - Demonstration and Validation
- [ ] Demonstrate a live Gemini-backed client session against the background agent.
- [ ] Demonstrate disconnect/reconnect without losing agent state.
- [ ] Demonstrate shutdown and restart with VM state restored from persisted slab.

## Risks and Mitigations
- **Risk**: Persistence scope creep into non-restorable runtime objects.
  - **Mitigation**: keep a strict durable/transient boundary and fail clearly on unsupported payload classes.
- **Risk**: Protocol ambiguity between session exit and full process shutdown.
  - **Mitigation**: define explicit commands and document lifecycle semantics.
- **Risk**: Hidden coupling between provider runtime state and persisted VM state.
  - **Mitigation**: persist logical state only; rebuild provider/request machinery after restore.

## Next Action
Create a dedicated architecture work product that turns this decision into a concrete runtime, lifecycle, and demo plan.