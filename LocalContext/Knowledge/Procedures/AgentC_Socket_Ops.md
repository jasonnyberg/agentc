# Knowledge: AgentC Socket Interface

## Overview
- **Socket Mode**: Launched via `--socket <PATH>`.
- **Advantages**: Full-duplex communication, no deadlocking on open/connect, robust state persistence, multi-client support.
- **Protocol**: Raw Edict command/response stream over Unix Domain Socket.
- **Lifecycle**: VM binds socket, accepts one connection, executes REPL. Shutdown via `exit` command.
