# Goal: G074 — Real-time FFI Token Streaming

**Status**: PLANNED  
**Created**: 2026-05-06  

## Objective
Extend the Cartographer FFI bindings (`libagent_runtime.so`) and the Edict logical execution engine to support real-time token streaming via Server-Sent Events (SSE) or native stream chunks.

## Rationale
Currently, the runtime wait for complete LLM responses before mutating the `Listree` memory and returning control. To support highly responsive UIs and human-in-the-loop interruptions, the Edict runtime must yield stream tokens dynamically.

## Acceptance Criteria
- [ ] Define an FFI streaming contract (e.g., generator-based or callback-based) over `agentc_call`.
- [ ] Implement SSE streaming parsing in `libagent_runtime` that pushes tokens over the FFI boundary.
- [ ] Edict scripts can process and forward chunks to the host I/O conduit in real-time.
