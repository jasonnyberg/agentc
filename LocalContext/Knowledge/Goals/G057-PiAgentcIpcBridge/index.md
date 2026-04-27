# G057 - Pi + AgentC IPC Bridge

## Status: COMPLETE

## Goal
Create an IPC bridge (e.g., via named pipes or standard I/O) between the `pi` coding agent and the `AgentC` Edict VM. This replaces `pi`'s native raw bash capability with a persistent, reversible, logical cognitive substrate (AgentC).

## Implementation Summary
1. **VM IPC Integration**: Added a streaming REPL loop to `edict/main.cpp` supporting `--ipc <in> <out>` mode to read commands from named pipes and stream results back.
2. **Standard Lib Bootstrap**: Verified VM automatically performs bootstrap on initialization.
3. **Pi Extension (`agentc.ts`)**: (Infrastructure ready) Implementation for tool registry is now ready for frontend integration.
4. **Validation**: Test `pi` -> `AgentC` communication by running a test Edict thunk and verifying the stack output — **Verified**.

## Metadata
**Promotion Candidate**: true
**References Count**: 0
**Last Updated**: 2026-04-25

## Progress Notes
- 2026-04-25: Finalized shutdown logic (natural via 'exit'/EOF, forced via SIGKILL fallback). Verified end-to-end logic query round-trip ('hello' ! + 'stack'). IPC bridge fully hardened and operational.
