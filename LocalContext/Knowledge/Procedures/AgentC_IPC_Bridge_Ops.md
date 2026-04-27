# AgentC Cognitive Substrate Integration Knowledge

## Overview
The `AgentC` substrate provides a persistent, logical cognitive layer for the `pi` agent. By leveraging a named-pipe (FIFO) based IPC bridge, `pi` can communicate directly with a headless `edict` VM. This architecture allows `pi` to maintain a persistent VM state (dictionaries, MiniKanren logic context) across multiple interactions.

## IPC Protocol
- **Ingress (`/tmp/agentc_in.pipe`)**: Standard FIFO. Pi writes command streams here using filesystem `write` primitives (e.g., `write` tool or `cat`).
- **Egress (`/tmp/agentc_out.pipe`)**: Standard FIFO. Pi reads output streams here using streaming tools (e.g., `cat` or `tail -f`). 
- **⚠️ Critical Restriction**: Do **not** use the agent's `read` tool on the egress pipe, as it performs an `lseek()` syscall which triggers an `ESPIPE` (illegal seek) error on non-seekable streams.

## Operational Patterns
- **Persistence**: The VM is launched with `--ipc`. It maintains the dictionary and stack state until an `exit` command is sent or the process is killed.
- **Direct Interaction**: Pi can interact with the VM without high-level runtime wrappers by simply treating the pipe as a write-only file (ingress) and a stream-only file (egress).
- **Graceful Shutdown**: Always terminate by writing `exit` or closing the input stream (`EOF`). `SIGKILL` is a fallback, not a standard shutdown mechanism.

## Integration Knowledge
- **Persistence Strategy**: The VM's state is held in-memory by the `edict` process. To persist state across process restarts, `pi` must use `agentc.export()` to serialize the VM state as JSON.
- **MiniKanren Capability**: Logic evaluations are performed via the `logicffi.agentc_logic_eval_ltv !` thunk, which maps standard relational logic queries into the VM execution context.
- **Verification**: The system has been validated via end-to-end integration tests (C++ GTest) and direct shell I/O testing, ensuring full round-trip logic capability.
