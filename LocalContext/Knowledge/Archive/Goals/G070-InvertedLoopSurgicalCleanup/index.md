# Goal: G070 - Inverted Loop Surgical Cleanup

## Goal
Execute a focused cleanup pass on the current inverted-loop implementation so the embedded Edict VM becomes the practical owner of turn/state transitions, transient runtime artifacts gain an explicit rehydration boundary, and native root persistence no longer depends on host-side JSON rematerialization.

## Status
**COMPLETE**

## Rationale
G068 established the target architecture clearly: keep the reconnectable **Client ↔ Host** boundary, keep **Host + Edict VM** embedded in one process, make the **canonical agent loop Edict-native**, and treat runtime/provider handles as **transient rehydrated state** rather than durable VM state.

Recent implementation work moved decisively in that direction:
- `libagent_runtime.so` now owns provider/auth/HTTP mechanics behind a reusable native boundary.
- Edict can import that runtime through `agentc.edict` and already has live POCs for hello-world turns, stateful conversation history, and a canonical agent-root object.
- The host now restores and persists a canonical root and can construct an embedded `EdictVM` around a restored anchored root.

But the production path still carries transitional host-owned orchestration that risks hardening into the long-term architecture:
- older host-owned `shared_root` / request-build / response-apply scaffolding existed in the normal prompt path and motivated this cleanup goal.
- `cpp-agent/runtime/persistence/agent_root_state.*` still contains host-side helpers for request building and response application — useful as a bridge, but too close to canonical policy ownership.
- the native-root persistence path was previously JSON-centered and has now been corrected, but startup/reset ownership and transient runtime/import rehydration still need to become more explicit and architecturally clean.

This goal exists to convert that transitional scaffolding into a sharper, smaller host boundary before it becomes entrenched. The point is not broad refactoring for its own sake; it is a targeted cleanup that preserves momentum while removing the highest-leverage sources of host-owned agent behavior.

## Scope
- Move the production turn/state transition path closer to VM/Edict ownership.
- Define the durable-vs-transient rehydration contract for runtime handles/imports around embedded VM restore.
- Simplify native root persistence so normal save/restore paths do not trampoline through JSON rematerialization.
- Retire or demote host-side canonical-loop helpers once the VM/Edict path is ready.

## Non-Goals
- Do not reintroduce a Host ↔ VM IPC boundary.
- Do not widen model-directed native/FFI capabilities beyond the current data-first policy.
- Do not redesign the entire runtime/provider layer; this goal is about ownership boundaries, not replacing the new runtime extraction.
- Do not delete transitional shims prematurely if they are still needed for build stability during the cleanup.

## Acceptance Criteria
- [x] The host no longer treats a host-owned JSON root as the canonical source of turn state.
- [x] The production turn path enters the embedded VM/Edict layer for request shaping and response-to-state mutation.
- [x] Durable root state stores only declarative runtime/import metadata; transient runtime handles/import bindings are rehydrated explicitly on startup/restore.
- [x] `SessionStateStore::saveRoot(...)` and the corresponding restore path persist native rooted state without a normal-path JSON serialize/reset/rematerialize trampoline.
- [x] Transitional host-side loop helpers are either removed, reduced to compatibility-only roles, or clearly demoted behind the VM-owned production path.
- [x] Existing demos/tests for runtime calls, root persistence, and embedded VM restore continue to pass after each cleanup slice.
- [x] New non-ephemeral regression coverage is added for the landed cleanup slices, especially VM-owned turn execution and native-root persistence behavior.

## Implementation Plan

### Phase 1 - Pin Down the Ownership Boundary
- [x] Document the concrete production ownership split for one full turn: host responsibilities, VM/Edict responsibilities, runtime-library responsibilities.
- [x] Identify the exact host-owned code paths that still perform canonical loop work (`shared_root`, request shaping, response application, session reset behavior).
- [x] Define the rehydrated artifact set explicitly: runtime handles, imported module bindings, parser/client state, sockets, and any other non-durable runtime objects.

Phase 1 target: make the cleanup executable as a sequence of narrow removals rather than a vague “move logic inward” directive.

Phase 1 evidence: 🔗[WP_G070_OwnershipBoundaryMap_2026-05-01](../../WorkProducts/WP_G070_OwnershipBoundaryMap_2026-05-01.md)

### Phase 2 - Make the VM/Edict Own the Production Turn Transition
- [x] Add or promote a VM/Edict entrypoint that accepts user input against the canonical root and performs the request-build / runtime-call / response-apply sequence inside the embedded VM boundary.
- [x] Change the host to submit input/control events to that entrypoint instead of mutating a host-owned canonical root directly.
- [x] Ensure host-visible reply text and persistence hooks are derived from the VM-owned root after the turn, not from a parallel host JSON state machine.

Phase 2 target: remove the architectural ambiguity where the VM exists but the host still performs the real loop transition.

### Phase 3 - Collapse Dual State Ownership
- [x] Eliminate `shared_root` as an independently authoritative copy of session state in `cpp-agent/main.cpp`.
- [x] Rework reset/startup flows so canonical state is created/restored once, then owned by the embedded VM/root.
- [x] Keep any host-side JSON materialization strictly observational/debugging if it is still needed.

Phase 3 target: the VM/root becomes the sole practical source of truth for durable agent state.

### Phase 4 - Clean Up Native Root Persistence
- [x] Replace the current `saveRoot(...)` JSON serialize/reset/rematerialize path with a direct native-root persistence path that preserves the intended slab/Listree semantics.
- [x] Verify that allocator lifecycle assumptions are safe for a long-lived embedded VM process and do not invalidate live state during normal save operations.
- [x] Add focused tests that exercise native-root save/restore without relying on JSON materialization as the proof mechanism.

Phase 4 target: persistence semantics match the architecture — native rooted state is what is being saved, not a host-side JSON reconstruction of it.

### Phase 5 - Retire Transitional Host Helpers
- [x] Reduce or remove host-side canonical-loop helpers in `agent_root_state.*` once the production VM/Edict path is stable.
- [x] Reassess compatibility shims and trim any that only existed to support the older host-owned loop path.
- [x] Update demos/tests/documentation so the primary story is the VM/Edict-owned production path, not the transitional host bridge.

Phase 5 target: the codebase becomes legibly aligned with G068 instead of merely containing G068-shaped proof-of-concept slices.

## Related Goals
- Primary architectural parent:
  - 🔗[G068 Client/Agent Split with Embedded Persistent Edict VM](../G068-ClientAgentSplitEmbeddedVmPersistence/index.md)
- Supporting persistence groundwork:
  - 🔗[G041 Persistent Slab Image Persistence](../G041_Persistent_Slab_Image_Persistence/index.md)
  - 🔗[G042 Persistent VM Root State and Restore Validation](../G042_Persistent_VM_Root_State_And_Restore_Validation/index.md)
  - 🔗[G069 Remove LMDB Dependencies](../G069-RemoveLMDBDependencies/index.md)
- Relevant work products:
  - 🔗[WP_EdictNativeAgentModuleArchitecture](../../WorkProducts/WP_EdictNativeAgentModuleArchitecture.md)
  - 🔗[WP_EmbeddedPersistentAgentArchitecture](../../WorkProducts/WP_EmbeddedPersistentAgentArchitecture.md)
  - 🔗[WP_CppAgentRuntimeFileStructure](../../WorkProducts/WP_CppAgentRuntimeFileStructure.md)
  - 🔗[WP_G070_OwnershipBoundaryMap_2026-05-01](../../WorkProducts/WP_G070_OwnershipBoundaryMap_2026-05-01.md)

## Risks and Mitigations
- **Risk**: The cleanup removes host scaffolding faster than the VM/Edict path can replace it.
  - **Mitigation**: land the work as thin slices with stable tests/demos after each ownership shift.
- **Risk**: Persistence surgery destabilizes allocator assumptions or restore invariants.
  - **Mitigation**: keep focused native-root save/restore tests in lockstep with implementation changes and avoid coupling save operations to global allocator resets.
- **Risk**: The rehydration boundary remains implicit even after code movement.
  - **Mitigation**: document the transient artifact set first, then make startup/restore code visibly rebuild only that set.
- **Risk**: Transitional compatibility layers survive indefinitely.
  - **Mitigation**: treat them as explicitly temporary and prune them once the VM-owned production path is the default path.

## Progress Notes

### 2026-05-02
- Made the transient runtime/import boundary more explicit in code instead of leaving it purely implicit in host startup: `cpp-agent/runtime/persistence/agent_root_vm_ops.*` now exposes `rehydrate_vm_runtime_state(...)`, which normalizes the VM-owned root, clears transient runtime-call scratch keys, and stamps only declarative runtime/import rehydration metadata (`runtime.rehydration`) back into the durable root.
- `cpp-agent/main.cpp` now calls that helper explicitly on both startup and `reset-session`, with lifecycle labels (`startup-fresh`, `startup-restored`, `session-reset`) and operator-config/source hints, so the host is no longer silently relying on ad hoc root normalization without an explicit rehydration step.
- Added durable regression coverage proving the explicit contract is real: `cpp-agent/tests/agent_root_vm_ops_test.cpp` now checks that rehydration preserves durable conversation state while removing transient runtime-call scratch keys, and `cpp-agent/tests/session_state_store_test.cpp` now proves the persisted root keeps only declarative rehydration metadata rather than transient runtime handles.
- Result: the durable-vs-transient contract is materially clearer now, though the remaining startup/reset ownership cleanup in `main.cpp` and the eventual retirement/demotion of older host-side helper surfaces still remain.

### 2026-05-01
- Created this goal to capture the surgical cleanup plan identified after reviewing the accumulated inverted-loop changes against G068.
- Decided to prioritize three specific architectural corrections: (1) reduce host-owned turn/state mutation, (2) define explicit transient runtime rehydration around VM restore, and (3) remove JSON rematerialization from normal native-root persistence.
- Started Phase 1 by producing 🔗[WP_G070_OwnershipBoundaryMap_2026-05-01](../../WorkProducts/WP_G070_OwnershipBoundaryMap_2026-05-01.md), which maps the current production turn path, identifies the still-host-owned canonical loop work, and enumerates the transient artifact set that should be rehydrated around restore.
- Result: Phase 1 is now complete enough to drive a narrow implementation slice instead of a vague architecture discussion.
- Landed the first concrete Phase 2 slice: `cpp-agent/main.cpp` no longer uses a host-owned `shared_root` for normal prompt turns; instead it asks the embedded VM to shape the runtime request and apply the runtime response through new helpers in `cpp-agent/runtime/persistence/agent_root_vm_ops.*`.
- Tightened that slice further by adding a VM-owned production turn helper (`run_vm_agent_root_turn(...)`) and deriving host-visible reply text from the VM-owned root via `reply_text_from_vm_agent_root(...)`, so the host now submits a normal prompt to one production turn entrypoint instead of manually orchestrating request/response/root-update pieces itself.
- Moved the runtime-call boundary further inward by adding `run_vm_agent_root_turn_via_imported_runtime(...)`: the production host now hands the prompt plus base runtime config/import artifacts to a VM-owned turn helper, which bootstraps the imported `agentc` runtime wrappers inside an embedded VM context and issues the runtime request from there instead of having the host call `agentc_runtime_request_json(...)` directly.
- Added durable regression coverage in `cpp-agent/tests/agent_root_vm_ops_test.cpp`, including direct checks that request shaping, full-turn execution through the runtime invoker callback, imported-runtime full-turn execution through a mock shared library, response-to-root mutation, VM-derived reply text, and error responses all preserve the canonical root correctly.
- Current status: Phase 2 is now functionally complete for the production normal-prompt path. The remaining work has shifted to startup/reset ownership cleanup plus making transient runtime/import rehydration more explicit and durable.
- Began that post-Phase-2 persistence groundwork by introducing session-isolated slab storage layout: `SessionStateStore` now persists each session under its own named subdirectory, and `cpp-agent/main.cpp` now accepts `--session` / `AGENTC_SESSION` so future mmap-backed slab files can live under per-session directories instead of one flat shared filename prefix.
- Identified that the remaining persistence work is now substrate-level enough to justify its own dedicated goal: 🔗[G071 Session-Scoped Allocator Image Persistence](../G071-SessionScopedAllocatorImagePersistence/index.md) captures the generic session-image / allocator-index / mmap-backed ownership direction beneath G070's remaining Phase 4/6 cleanup.
- Landed the key native-root persistence cleanup slice through G071: `SessionStateStore` now saves through `cpp-agent/runtime/persistence/session_image_store.*`, persists manifest/index-driven allocator images, and `saveRoot(...)` no longer serializes the root to JSON and rematerializes it before persistence. Instead it creates a native checkpointed snapshot via allocator checkpoints + `ListreeValue::copy()` + rollback, then writes slab images plus a root anchor without disturbing the live VM/root state.
- Added focused regression coverage for that slice in `cpp-agent/tests/session_state_store_test.cpp`, proving native snapshot save preserves live ambient allocator state while still restoring a structurally equivalent canonical root.
- Next: treat G071 as the persistence-substrate execution vehicle for Phase 3 mmap-file ownership work while G070 continues to track the architecture-alignment consequences above it, especially startup/reset ownership cleanup and explicit transient runtime rehydration.

## Next Action
Use 🔗[G071 Session-Scoped Allocator Image Persistence](../G071-SessionScopedAllocatorImagePersistence/index.md) to shape the slab/session layout for mmap-backed authoritative ownership, then feed that substrate back into G070's remaining startup/reset ownership and transient rehydration cleanup work.

### 2026-05-02 (Session 2)
- Removed `normalize_agent_root` completely from `apply_runtime_response_to_vm_agent_root`, eliminating the JSON trampoline that was destroying live Listree pointers on every normal turn.
- Moved the remaining bootstrapping JSON helpers (`make_default_agent_root`, etc.) into `agent_root_vm_ops.*` so they are strictly used for initial creation and rehydration.
- Deleted `agent_root_state.h`, `agent_root_state.cpp`, and their tests, fulfilling Phase 5 (Retire Transitional Host Helpers) and proving the host has cleanly handed canonical loop state to the VM without relying on the older host C++ loop utilities.

- Cleaned up the file tree by entirely deleting unused legacy logic (`agent_loop.*`, `edict_tools.*`, `api_registry.*`, `credentials.*`, `http_client.*`, `sse_parser.*`, `hello_gemini.cpp`, etc.) and their test cases.
- Moved `ai_types.h` explicitly inside the `runtime/core/` tree to signify its scoped purpose.
- Verified that the remaining `cpp-agent/demo/` scripts correctly reflect the VM/Edict-owned production path.