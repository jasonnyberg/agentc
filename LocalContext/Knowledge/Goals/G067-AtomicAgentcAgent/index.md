# Goal: G067 - Atomic AgentC Agent Transaction (Request-Response Lifecycle)

## Rationale
To ensure the `cpp-agent` behaves as a robust, atomic tool rather than a long-running, crash-prone daemon. The agent should accept one request (via socket), perform the reasoning/tool-call cycle, return the final result, and exit cleanly.

## Acceptance Criteria
- [ ] **Atomic Execution**: The agent processes one input turn from the socket, executes the full AgentLoop (including tool dispatching), and returns the final assistant message.
- [ ] **Clean Socket Closure**: Upon completing the response, the agent closes the socket and terminates gracefully (no `std::system_error` or "Aborted" logs).
- [ ] **Integration Test**: A verification test demonstrates a complete cycle: Input (Prompt) -> AgentLoop -> VM Tool Call -> Result -> Agent Output -> Exit.

## Implementation Steps
1. **Refactor `main.cpp`**: Remove the `while(true)` loop; implement a single-request handler that triggers one `run_agent_loop` turn.
2. **Handle Socket Lifecycle**: Ensure `asio` gracefully shuts down the socket once the final event in the `AssistantMessageStream` is emitted.
3. **Verification**: Implement `tests/atomic_interaction_test.cpp` to confirm the full cycle works in a single `socat` session without crashes.
EOF
,path: