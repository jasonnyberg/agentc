# Goal: G073 — Pure VM Lifecycle & Host Thinning

**Status**: IN PROGRESS  
**Created**: 2026-05-06  

## Objective
Finalize the architectural transition started in G068. Strip the last remaining outer orchestration from `cpp-agent/main.cpp` and `agent_root_vm_ops.*` so that the `EdictVM` completely owns its own lifecycle, I/O routing, and state transition loops. The host process should become nothing more than a thin process supervisor and socket/pipe conduit.

## Rationale
With direct mmap slab restore (G072) and session-scoped persistence (G071) complete, the VM is fully capable of managing its own continuous state. Remaining host-side helpers and orchestration logic blur the boundary of responsibility, risking memory leaks or state desyncs if the C++ host mutates the anchored root outside of Edict's native execution loop.

## Acceptance Criteria
- [x] Remove all transient C++ cognitive loop overrides.
- [x] `cpp-agent/main.cpp` acts solely as an I/O listener and startup/shutdown supervisor.
- [ ] Any required root initialization or repair happens natively inside an Edict bootstrap script, not via C++ `Listree` manual tree building.
