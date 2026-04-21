# G057 - Pi + AgentC IPC Bridge

## Status: PROPOSED

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Architectural vision: 🔗[`../../../../README.md`](../../../../README.md) (AgentC as a cognitive substrate behind an outer tool-driven agent loop)
- Builds on AgentC's foundational capabilities: speculative execution, Cartographer FFI, and logic processing.

## Goal

Create an IPC bridge (e.g., via named pipes or child_process) between the `pi` coding agent and the `AgentC` Edict VM. This effectively replaces `pi`'s native raw bash capability with a persistent, reversible, logical cognitive substrate (AgentC), unlocking new workflows such as dynamic C++ extension compilation and import.

## Why This Matters

Current agentic AI systems (like `pi`) suffer from reasoning that disappears between turns, token-inefficient context tracking, and fragile bash-based state mutation. AgentC solves these by providing:
1. **Token-Efficient Shadow Memory:** `pi` can store structured state in AgentC's Listree graph using Edict paths (e.g., `'active' @agent.status`) instead of bloating the conversation window.
2. **Speculative Execution & Rollback:** Mutations and complex commands can be wrapped in `speculate [ ... ]`. If they fail, the state rolls back instantly (O(1) via the Slab allocator), preventing permanent workspace corruption.
3. **Relational Reasoning:** Instead of writing Python to resolve dependency graphs or diffs, `pi` can query AgentC's embedded `mini-Kanren` logic engine directly.
4. **Dynamic Native Extensions:** `pi` can write C++ files, invoke the compiler via AgentC's `system` bindings, and instantly load them using Cartographer FFI (`'./ext.so' './ext.h' resolver.import ! @ext`), getting high-performance capability on the fly.

## Proposed Implementation Path

1. **AgentC System Bindings:** Ensure AgentC exposes a `system` or `popen` equivalent to allow `pi` to still execute shell commands (like invoking `gcc`/`clang` or Git) *through* the AgentC context.
2. **IPC Integration:** Develop the named-pipe or `stdin`/`stdout` streaming wrapper on the AgentC side to support persistent REPL interactions without exiting.
3. **Pi Extension (`agentc.ts`):** Build a Pi extension (via `@mariozechner/pi-coding-agent` extension API) that:
    - Spawns AgentC as a persistent child process.
    - Registers a new tool `agentc_eval` (or replaces the default `bash` tool).
    - Exposes the Edict interaction model to the LLM.
4. **Prompt Template:** Provide `pi` with a specialized prompt template explaining Edict syntax, speculative boundaries, and the Cartographer capability model.

## Navigation Guide For Agents

- This is the critical integration step bringing the "Agent" back into AgentC.
- Keep the IPC layer minimal. Let Edict and Pi's TypeScript API do the heavy lifting.
- When generating C++ code for AgentC extensions, remember to emit standard `extern "C"` interfaces and Cartographer-friendly struct definitions.

## Metadata

**Promotion Candidate**: true
**References Count**: 0
**Last Updated**: 2026-04-18
