# Skill: AgentC Cognitive Substrate

## Description
Provides persistent, logical, and transactional cognitive state management using the AgentC Edict VM.

## Usage
- `agentc.eval(code: string)`: Executes Edict bytecode in the persistent VM session.
- `agentc.speculate(code: string)`: Executes code transactionally; returns the result or rolls back on error.
- `agentc.export()`: Serializes current VM state to JSON for context-window persistence.

## Dependencies
- Backend: `edict` (must be installed and in `PATH`).
- Configuration: `IPC_IN_PIPE`, `IPC_OUT_PIPE` environment variables.

## Protocol
- Handshake: The skill initializes the VM upon first access.
- Error Handling: All `Error:` responses from the VM are raised as JavaScript exceptions within the agent's context.

## References
- Contract: 🔗[AgentcEvalContract.md](../../LocalContext/Knowledge/Contracts/AgentcEvalContract.md)
