# AgentC Skill Handoff

## Overview
The `agentc` skill is now a formal agentic mechanism for interacting with the Edict VM.

## API Specification
- `AgentCSubstrate(in, out)`: Initializes the IPC connection.
- `eval(code)`: Non-blocking Edict execution.
- `speculate(code)`: Transactional Edict execution with automatic rollback on failure.

## Current State
- IPC logic is implemented using non-blocking `fs` streams.
- Transactional safety is enforced via `beginTransaction`/`commit`/`rollback`.
- Pending Resolve mechanism manages command/response correlation.

## Integration Notes
- Ensure `edict --ipc` is running with the correct pipe paths BEFORE initializing the substrate.
- The interface is currently line-based.
