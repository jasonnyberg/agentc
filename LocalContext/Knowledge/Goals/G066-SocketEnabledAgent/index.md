# HRM Plan: G066 - Socket-Enabled AgentC Agent (The "AgentC Daemon")

## Rationale
To transition from a transient process to a persistent, attachable "AgentC Agent" entity. By moving to a Unix Domain Socket (UDS) server, we decouple the LLM provider loop (the daemon) from the user interface (the client), enabling hot-attach/detach, state persistence, and long-running background tasks. This is the foundation for "Semantic Anchoring"—where the agent can define new vocabulary in the VM and maintain that vocabulary across disconnected sessions.

## Acceptance Criteria
- [ ] `cpp-agent` runs as a background process listening on `/tmp/agentc.sock`.
- [ ] User can connect via `socat` to open an interactive session.
- [ ] Agent state (VM dictionary) persists after client disconnection.
- [ ] Reconnecting to the socket restores the previous session context.
- [ ] `asio` integrated via `FetchContent` and stable in build.

## Implementation Steps

### 1. Networking Infrastructure
- [ ] Add `asio` (header-only) to `cpp-agent/CMakeLists.txt`.
- [ ] Implement `cpp-agent/socket_server.h` / `.cpp` to manage the `asio` acceptor loop.
- [ ] Refactor `main.cpp` to initialize the `AgentC` VM once, then enter a `while(true)` accept loop.

### 2. Stream Multiplexing
- [ ] Map `AgentEventSink` to a buffered socket writer.
- [ ] Implement a simple command parser to bridge the socket input to the agent loop.

### 3. Session Persistence
- [ ] Ensure `AgentContext` and `EdictVM` instances are created outside the accept loop to survive client disconnects.
- [ ] Verify `Edict` dictionary state is retained across disconnect/reconnect cycles.

## Risk Assessment
- **High**: Concurrent access. *Mitigation*: The loop is synchronous; only one client can interact at a time (sufficient for initial launch).
- **Medium**: Socket cleanup. *Mitigation*: Ensure `unlink("/tmp/agentc.sock")` is called on process termination.
EOF
,path: