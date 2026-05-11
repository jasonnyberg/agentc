# Goal: G076 — Generalized Multiline Edict Accumulator

**Status**: COMPLETE  
**Created**: 2026-05-09  
**Completed**: 2026-05-09

## Objective
Enable Edict module scripts to be written in a readable, multiline format by implementing an accumulator that buffers script lines based on bracket depth `[]`, `()`, `{}` before compiling them.

## Rationale
Edict's parser previously required entire script definitions to be written on a single line because the REPL runner compiled line-by-line. The language itself supported multiline logic, but the delivery mechanism did not.

## Acceptance Criteria
- [x] Replace the line-by-line compile loop in `EdictREPL::runScript()` with an accumulator.
- [x] Refactor module files (`agentc_stateful_loop.edict`, `agentc_agent_root.edict`) to use multiline, indented definitions with `f(args)` functional isolation.
- [x] Isolate the accumulator into a generalized `BlockAccumulator` component inside `libedict` so it can be reused for FFI streams or network stream accumulation.
