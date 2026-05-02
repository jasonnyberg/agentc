# WP: G070 Ownership Boundary Map (2026-05-01)

## Purpose
Make G070 Phase 1 concrete by documenting:
1. the current production ownership split for one interactive turn,
2. the exact host-owned code paths that still perform canonical loop work, and
3. the transient artifact set that must be rehydrated around embedded VM restore.

## Bigger Picture
This work product supports:
- 🔗[G070 Inverted Loop Surgical Cleanup](../Goals/G070-InvertedLoopSurgicalCleanup/index.md)
- 🔗[G068 Client/Agent Split with Embedded Persistent Edict VM](../Goals/G068-ClientAgentSplitEmbeddedVmPersistence/index.md)

It exists to turn “move logic inward” into a bounded implementation plan.

## Current Production Turn Path
Primary implementation path:
- `cpp-agent/main.cpp`
- `cpp-agent/runtime/persistence/agent_root_state.cpp`
- `cpp-agent/runtime/persistence/session_state_store.cpp`

### Current sequence
1. Host starts, restores anchored root through `SessionStateStore::loadRoot(...)`.
2. Host constructs `EdictVM embedded_vm(initial_root_value)`.
3. Host immediately materializes VM state into host JSON via `root_json_from_vm_or_throw(...)` and `normalize_agent_root(...)`.
4. Host stores that JSON in `shared_root`.
5. Host configures the runtime from `shared_root["runtime"]`.
6. On user input, host handles control flow in `main.cpp`:
   - `exit`
   - `reset-session`
   - `shutdown-agent`
7. For a normal prompt, host performs the full canonical turn transition:
   - `build_request_from_agent_root(shared_root, input)`
   - `runtime_call_json(runtime, request)`
   - `json::parse(response_text)`
   - `extract_reply_text(response)`
   - `apply_runtime_response_to_agent_root(shared_root, input, response)`
8. Host then mirrors updated JSON back into the VM using `replace_vm_root_or_throw(embedded_vm, shared_root)`.
9. Host persists from the VM-owned root using `session_store.saveRoot(embedded_vm.getCursor().getValue(), ...)`.
10. Host writes reply text to the client socket.

## Ownership Map

### What the host should own
These responsibilities are aligned with G068 and should remain host-side:
- process lifecycle
- socket accept loop / client attachment
- control-command dispatch (`exit`, `shutdown-agent`, and possibly transport-level reset commands)
- startup/shutdown sequencing
- runtime-handle creation/configuration/rehydration
- persistence orchestration (triggering save/restore), but not canonical cognitive-state mutation
- rendering reply text to connected clients

### What the VM / Edict layer should own
These responsibilities are currently only partially true and should become the production path:
- canonical root/state ownership
- request shaping from canonical conversation/root state
- response-to-state mutation
- turn bookkeeping (`messages`, `last_prompt`, `last_response`, `assistant_text`, loop status)
- policy/orchestration about how a user prompt becomes an LLM turn
- reset semantics for agent-state fields, if reset is intended to mean “reset the canonical agent root” rather than “host clears an auxiliary cache”

### What the native runtime library should own
These responsibilities are already mostly correct:
- provider selection
- auth/credentials
- HTTP transport
- provider response normalization
- stable JSON C ABI surface

## Concrete Host-Owned Canonical Loop Work Still Present

### In `cpp-agent/main.cpp`
The host is still performing canonical loop work in these places:
- startup: creating `shared_root` from the VM root and then re-injecting it back into the VM
- reset path: rebuilding the default canonical root in host JSON and replacing the VM root from that JSON
- normal prompt path:
  - `build_request_from_agent_root(shared_root, input)`
  - `apply_runtime_response_to_agent_root(shared_root, input, response)`
  - `replace_vm_root_or_throw(embedded_vm, shared_root)`

This means the VM is currently persistence-coupled but not yet the sole live owner of canonical turn transitions.

### In `cpp-agent/runtime/persistence/agent_root_state.cpp`
These helpers encode canonical loop-policy behavior and are therefore transitional, not ideal end-state host responsibilities:
- `build_request_from_agent_root(...)`
- `apply_runtime_response_to_agent_root(...)`

These functions are useful migration scaffolding, but they are exactly the behaviors G070 intends to move behind the VM/Edict-owned production path.

### In `cpp-agent/runtime/persistence/session_state_store.cpp`
`saveRoot(...)` currently does:
- `agentc::toJson(root)`
- `resetStructuredListreeAllocators()`
- `agentc::fromJson(rootJson)`
- persist newly materialized slabs

This is architecturally misaligned with the intended “native rooted state is canonical” durability story.

## Transient Rehydrated Artifact Set
These objects should be treated as rebuild-on-restore artifacts rather than durable root state:
- `agentc_runtime_t runtime` handle in `cpp-agent/main.cpp`
- Cartographer/import bindings needed for `agentc.edict` runtime access
- dynamic-library handles / FFI import state
- function pointers / imported builtin bindings
- open client sockets and acceptor state
- active HTTP request/stream state
- SSE parser buffers
- provider client/session state
- thread-local runtime state

## Durable Root Metadata That Should Remain Persisted
The persisted root may still carry declarative metadata needed to rehydrate transient state:
- default provider/model selection
- runtime configuration values intended to survive restart
- import/module configuration metadata if needed to rebuild bindings deterministically
- policy flags that constrain runtime/tool behavior

The root should not attempt to persist live handles, pointers, sockets, or active network/parser state.

## Immediate G070 Consequences

### Phase 1 result
Phase 1 can be considered complete once this map is captured in project memory and referenced from G070.

### Recommended next code slice
The smallest next architecture-improving slice is:
1. introduce or promote a VM/Edict turn entrypoint that accepts input against the canonical root,
2. call that entrypoint from `main.cpp` instead of using `build_request_from_agent_root(...)` / `apply_runtime_response_to_agent_root(...)`,
3. keep host JSON materialization, if still needed, strictly observational rather than authoritative.

### Regression expectation
Any Phase 2+ slice should land with durable regression coverage, ideally extending `cpp_agent_tests` with checks that:
- the production turn path is VM-owned rather than host-owned,
- persisted root save/restore does not regress,
- host control commands continue to behave correctly.
