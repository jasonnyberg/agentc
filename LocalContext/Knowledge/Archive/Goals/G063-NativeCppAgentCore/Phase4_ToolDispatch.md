# Phase 4: Tool Call Dispatch - Acceptance Criteria

## Goal
Integrate the AgentC VM's tool call dispatching logic with the native C++ agent loop.

## Sub-Goals & Acceptance Criteria

### 1. Tool Call Registration Infrastructure
- **Sub-Goal**: Expose C++ tool definitions to the Edict VM.
- **Acceptance Criteria**: 
    - [ ] `api_registry` successfully maps C++ functions to Edict-callable thunks.
    - [ ] Edict VM recognizes registered tool names via a lookup function.
    - [ ] Verification: A test exists that registers a tool in C++ and invokes it via Edict bytecode.

### 2. Event Loop Integration (EvToolExecStart/End)
- **Sub-Goal**: Update `agent_loop.cpp` to correctly handle `ToolCall` events from the LLM provider.
- **Acceptance Criteria**:
    - [ ] The agent loop pauses upon receiving a `ToolCall` event.
    - [ ] The loop dispatches the call to the appropriate C++ handler.
    - [ ] The loop waits for the tool execution result.
    - [ ] The loop resumes by injecting the `ToolResult` message back into the context.

### 3. End-to-End Tool Dispatch Test
- **Sub-Goal**: A successful tool execution cycle in the native agent.
- **Acceptance Criteria**:
    - [ ] A mock LLM provider is configured to emit a `ToolCall` event.
    - [ ] The agent correctly catches this call, dispatches it to a C++ tool.
    - [ ] The C++ tool returns a success result.
    - [ ] The agent emits `EvToolExecStart` and `EvToolExecEnd` events correctly.
    - [ ] The agent turn completes after receiving the tool result.

## Progress Tracking
- [ ] 4.1 Tool Registration
- [ ] 4.2 Event Loop Dispatch
- [ ] 4.3 End-to-End Validation
EOF
,path: