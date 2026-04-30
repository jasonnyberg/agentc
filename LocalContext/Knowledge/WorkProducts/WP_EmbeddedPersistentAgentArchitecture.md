# Work Product: Embedded Persistent Agent Architecture

## Purpose
Capture the agreed AgentC runtime architecture: reconnectable client/agent separation, embedded Edict VM, and memory-mapped slab persistence as the primary durability mechanism.

## Summary
The target system keeps the **interactive client outside** the long-lived agent process while keeping the **Edict VM embedded inside** the agent process. Persistence is achieved by restoring VM state from a memory-mapped slab image rather than by maintaining a separate always-on VM daemon.

## Target Runtime Shape
```text
+---------------------------------------------------+
| Client / TUI / CLI                                |
| - attach / detach / reconnect                     |
| - send prompts and control commands               |
| - render replies                                  |
+------------------------|--------------------------+
                         | UDS / stream I/O
                         v
+---------------------------------------------------+
| AgentC Agent Process                              |
| - socket server                                   |
| - session manager                                 |
| - agent loop                                      |
| - LLM provider adapters                           |
| - tool dispatch                                   |
| - checkpoint / restore manager                    |
| - embedded Edict VM                               |
+-------------------|-------------------|-----------+
                    |                   |
                    | in-process API    | HTTPS JSON/SSE
                    v                   v
          +------------------+    +-------------------+
          | Embedded Edict VM|    | Gemini / OpenAI   |
          | - durable state  |    | provider APIs     |
          | - memory/logic   |    +-------------------+
          | - tool substrate |
          +--------|---------+
                   |
                   | mmap-backed slab image
                   v
          +--------------------------+
          | Persistent Slab File(s)  |
          +--------------------------+
```

## Core Architectural Rules
1. **Keep Client and Agent separate.**
   - The terminal/UI can be restarted without killing the agent.
2. **Keep Agent and Edict VM together.**
   - No new Agent↔VM IPC layer unless future requirements force it.
3. **Persist logical VM state, not runtime handles.**
   - Rehydrate FFI and OS-owned runtime artifacts after restore.
4. **Prefer fast restart over separate backend complexity.**
   - mmap slab persistence is the primary recovery mechanism.

## Communication Means

### Client ↔ Agent
- Unix domain socket (`/tmp/agentc.sock`) initially.
- Stream-oriented request/reply protocol.
- Future extensions may include multiplexed observers or multiple sessions.

### Agent ↔ Provider APIs
- HTTPS via `libcurl`.
- JSON request/response for simple correctness path.
- SSE only where streaming materially improves the UX and correctness is preserved.

### Agent ↔ Edict VM
- In-process C++ calls.
- Direct memory and API boundary.
- No socket, pipe, or stream bridge on the hot path.

### Edict VM ↔ Persistence
- mmap-backed slab image.
- restore hooks for roots/anchors and supported durable payloads.
- explicit rehydration path for transient artifacts.

## Lifecycle Model

### Start Agent
1. Launch agent process.
2. Open or initialize slab-backed VM state.
3. Restore VM roots/anchors if a persisted image exists.
4. Rehydrate transient runtime dependencies.
5. Begin accepting client connections.

### Client Session
1. Client connects to socket.
2. Agent serves prompts over the active embedded VM state.
3. Client may detach with no VM teardown.
4. Later clients may reconnect to the same agent instance.

### Shutdown Agent
1. Agent receives explicit shutdown command or system signal.
2. Agent quiesces session activity.
3. VM checkpoints to slab.
4. Process exits.

### Restart Agent
1. Relaunch process.
2. Restore VM from slab.
3. Rehydrate transient state.
4. Accept new client connections.

## Durable / Transient Boundary

### Durable
- cognitive state,
- messages worth preserving,
- goals/facts/working memory,
- VM roots and anchors,
- persistence-safe tool metadata,
- resumable task state.

### Transient
- dlopen handles,
- function pointers,
- sockets,
- active HTTP requests,
- in-flight stream parsers,
- thread-local execution artifacts.

## Protocol Semantics
- `exit`: close current client session only.
- `shutdown-agent`: persist state and stop the background agent process.
- future: `status`, `list-sessions`, `attach`, observer mode.

## Demonstration Shape
A complete architecture demo should show:
1. start the background agent,
2. attach a client and talk to Gemini,
3. detach the client,
4. reattach and verify state continuity,
5. shut down the agent,
6. restart and verify restored VM-backed state.

## Open Questions
- Whether multiplexing should be observer-only first or permit concurrent writers.
- Whether checkpointing is only on explicit shutdown or also periodic/incremental.
- What exact persisted metadata is needed to rehydrate FFI imports safely.

## Recommendation
Proceed with this architecture as the primary runtime direction for post-G067 AgentC work.