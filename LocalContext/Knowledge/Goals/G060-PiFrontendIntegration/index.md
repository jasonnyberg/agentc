# G060 - Pi Frontend Integration
## Goal
Implement the `agentc.ts` Pi extension, establish the IPC management layer, and validate the `agentc_eval` tool registry.

## Implementation Steps
1. Create `agentc.ts` to manage the lifecycle of the `edict` child process.
2. Implement `executeEdictViaPipe` to handle non-blocking stream communication.
3. Hook into Pi's tool registry to replace standard bash calls with `agentc_eval` where appropriate.
4. Integrate the provided `agentc.ts` snippet into the Pi environment.
5. Implement Unix Domain Socket mode for persistent, non-blocking VM interaction.
6. Validate end-to-end Pi/Edict socket communication and persistence.

## Progress Notes
- 2026-04-26: Refactored AgentCSubstrate to support protocol-agnostic stream.Duplex transport. Implemented socket-based factory method for persistent VM interaction.

## Progress Notes
- 2026-04-26: Verified Mini-Kanren query execution over Unix Domain Socket IPC. Confirmed successful pathing for  and  capability.
