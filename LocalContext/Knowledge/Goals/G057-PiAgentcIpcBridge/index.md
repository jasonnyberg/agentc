# G057 - Pi + AgentC IPC Bridge

## Status: IN PROGRESS

## Goal
Create an IPC bridge (e.g., via named pipes or standard I/O) between the `pi` coding agent and the `AgentC` Edict VM. This replaces `pi`'s native raw bash capability with a persistent, reversible, logical cognitive substrate (AgentC).

## Implementation Plan
1. **VM IPC Integration**: Add a streaming REPL loop to `edict/main.cpp` (or a dedicated IPC service) to read commands from stdin/named pipe and stream results back.
2. **Standard Lib Bootstrap**: Ensure the IPC-enabled VM instance automatically pre-loads the `extensions/` standard library and the Cartographer capability layer.
3. **Pi Extension (`agentc.ts`)**: Implement the Pi tool registry and pipe-handling logic.
4. **Validation**: Test `pi` -> `AgentC` communication by running a test Edict thunk and verifying the stack output.

## Metadata
**Promotion Candidate**: true
**References Count**: 0
**Last Updated**: 2026-04-25
