# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-04-27

## Status
- G057 (Pi + AgentC IPC Bridge) - **COMPLETE**
- G060 (Pi Frontend Integration) - **COMPLETE**
- G061 (AgentC Stability and Hardening) - **COMPLETE**
- G062 (Logic Engine Bootstrapping) - **CANCELLED**
- G063 (Native C++ Agent Core) - **COMPLETE** 🔗[index](./Knowledge/Goals/G063-NativeCppAgentCore/index.md)

## Current Focus
**Active Goal**: G063 — Native C++ Agent Core  
**Phase 1**: COMPLETE — build scaffolds, all C++ types, EventStream  
**Phase 2**: COMPLETE — credentials.cpp (Google/OpenAI env vars, Copilot auth.json + token refresh)  
**Phase 3**: NOT STARTED — mock StreamFn, agent loop test, tool call dispatch test  

## Handoff Note

**Project**: Build a minimal C++ AI agent (Google/OpenAI/GitHub Copilot) with the Edict VM
linked in-process. No hardcoded tools. All capabilities via Cartographer FFI.

**Current State**: Phases 1 and 2 complete. `./build/cpp-agent/cpp-agent` builds and runs.
All type definitions are in place. Credential layer implemented including Copilot OAuth
token refresh. All non-edict tests pass (edict_tests has pre-existing timeout, unrelated).

**Next Action — EXECUTE IMMEDIATELY**:
Implement Phase 3: add a mock StreamFn to `cpp-agent/main.cpp` that returns a hardcoded
`EvDone` event, then call `run_agent_loop()` with it and verify a single-turn conversation
completes and events are emitted in the correct order. Then add a tool call test.

**Key Context**:
- Full plan: 🔗[G063](./Knowledge/Goals/G063-NativeCppAgentCore/index.md)
- Build: `cd build && make cpp-agent` (fast — ~10s)
- Run: `./build/cpp-agent/cpp-agent`
- Tests (use --timeout): `cd build && ctest --timeout 30 -E edict_tests`
- edict_tests has a pre-existing timeout issue — exclude it with `-E edict_tests`
- Phase 3 ref: `~/pi-mono/packages/agent/src/agent-loop.ts` lines 170–350 (runLoop)
- The mock StreamFn should call `stream.end(msg)` synchronously for simplicity
- EventStream is callback-based (on_event / on_complete / on_error)
- agent_loop.cpp calls stream_fn synchronously then waits for on_complete callback

**Do NOT**:
- Do not run `ctest` without `--timeout 30` — edict_tests hangs
- Do not add tools or providers yet — Phase 3 is mock/test only
- Do not modify existing AgentC source files
- Do not use C++20 features — project is C++17

## Important: Context Management
- Checkpoint (update Dashboard + Goal file) after EVERY phase or significant milestone
- Use `--timeout 30` on all ctest/make test commands
- If context window approaches 70%, checkpoint immediately and stop

## Session Compliance
1. Review Dashboard: ✅
2. Update Goals: ✅ (Phase 1+2 progress noted)
3. Persist Knowledge: ✅
4. Update Dashboard: ✅
5. Timeline Entry: ✅
6. Handoff Note: ✅
7. Session Checklist: ✅
