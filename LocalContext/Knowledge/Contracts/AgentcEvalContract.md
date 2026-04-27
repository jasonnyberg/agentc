# AgentC Interface Contract: `agentc_eval`

The Pi frontend interacts with the `AgentC` backend by invoking the `agentc_eval` tool, which communicates with the persistent `edict --ipc <in> <out>` process.

## Tool Interface
- **Tool Name**: `agentc_eval`
- **Parameter**: `edict_code` (string)
- **Behavior**: 
  1. The Pi extension writes the provided `edict_code` to the `in` pipe.
  2. The `edict` backend consumes the code via its `EdictREPL` loop.
  3. The `edict` backend writes the result/stack-state to the `out` pipe.
  4. The Pi extension reads the `out` pipe and returns the result to the agent.

## Communication Protocol
The protocol is line-based. To ensure deterministic results:
1. Pi writes a single line of Edict code followed by `\n`.
2. AgentC processes the line.
3. AgentC outputs the result prefixed by `=> `.
4. If an error occurs, AgentC outputs `Error: ...`.

## Example Interaction
**Pi (via `agentc_eval`):**
```
1 2 + !
```
**AgentC Backend Output (via `out` pipe):**
```
=> 3
```

## Contract Requirements
- **Process Persistence**: Pi must maintain a long-running instance of `edict --ipc` for the duration of the session to preserve dictionary/state frames.
- **Serialization**: All complex state sharing between Pi and AgentC should be performed using the `toJson` / `fromJson` utilities validated in G059.
- **Tool Registry**: All custom Thunks/FFI functions loaded via Cartographer must be registered before being called by the Pi frontend.
EOF
,path: