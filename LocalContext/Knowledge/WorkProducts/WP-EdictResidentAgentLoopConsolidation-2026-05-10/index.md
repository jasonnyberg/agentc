# Work Product: Edict-Resident Agent Loop Consolidation Plan (2026-05-10)

## Purpose
Capture the current duplication map and the concrete consolidation path for making the Edict VM the authoritative user-facing agent runtime.

## Current Layer Map

### 1. Shell / Demo Bootstrap
Files like `demo_local_llm.sh`, `demo_live_llm.sh`, and the `cpp-agent/demo/` scripts currently duplicate:
- library import bootstrap
- module load order
- config-path fallback logic
- provider/model selection examples
- system-prompt/demo prompt setup

### 2. Host Bootstrap / Lifecycle
`cpp-agent/main.cpp` still duplicates or owns:
- fallback runtime config creation (`default_runtime_config(...)`)
- fallback root creation via `make_default_agent_root(...)`
- live input loop and output formatting
- command dispatch for `exit`, `reset-session`, `shutdown-agent`
- per-turn persistence triggers

### 3. Edict Loop / Root Logic
`cpp-agent/edict/modules/agentc.edict`, `agentc_stateful_loop.edict`, and `agentc_agent_root.edict` already own:
- C ABI wrapping
- conversation state mutation
- request construction from native Edict state
- response application back into native Edict state

This is already the best architectural seam in the system.

### 4. Runtime Normalization / Resolution
`cpp-agent/runtime/core/runtime.cpp` still performs overlapping semantic work:
- request parsing and normalization
- `prompt` versus `messages` handling
- `system` injection
- provider/model/base-url fallback resolution
- request option extraction

Some of this belongs at the runtime execution boundary, but much of it overlaps with the desired Edict-owned control plane.

### 5. Provider Wire Adapters
`cpp-agent/runtime/providers/google/google_provider.cpp` and `.../openai/openai_provider.cpp` translate canonical runtime context into provider-native HTTP payloads.

This layer should remain provider-specific. The goal is not to erase adapter differences, but to ensure that all semantic request authorship above this point happens once.

## Key Duplication Problems

### Root Schema Duplication
- `agentc_state_init` defines conversation state in Edict.
- `agentc_agent_root_init_with_runtime` defines the canonical root in Edict.
- `make_default_agent_root(...)` redefines the same root schema in C++.

This creates drift risk and makes Edict non-authoritative over its own root shape.

### Config Authority Duplication
- `agentc-config.json` is the operator config file.
- demos sometimes embed provider/model literals directly.
- host options still maintain provider/model/system defaults.
- persisted root runtime data also carries defaults.

There are too many simultaneous "sources of truth" for runtime selection.

### Request Authorship Duplication
- `agentc_hello_loop.edict` builds `prompt` requests.
- `agentc_stateful_loop.edict` builds `system` + `messages` requests.
- `runtime.cpp` then re-normalizes those requests again.
- providers finally reshape the normalized context into wire payloads.

The problem is not the provider adapters. The problem is that the same semantic request is authored more than once before it reaches them.

### UX Loop Duplication
- the intended loop exists conceptually in Edict
- the actual user-facing socket loop is still in `main.cpp`

That split keeps UI policy, command handling, and presentation outside the canonical VM state.

## Target Architecture

### Edict Should Own
- canonical agent root construction
- runtime config loading/normalization into live Listree data
- request assembly for user turns
- command interpretation and UX loop state machine
- response projection and presentation policy
- session-local policy and memory decisions

### Runtime C ABI Should Own
- execution of canonical requests
- credential lookup and native secret handling
- provider dispatch
- provider-specific payload generation
- streaming infrastructure and transport-level diagnostics
- normalized success/error envelopes returned to Edict

### Host Should Own
- VM startup / restore / shutdown
- socket attach/detach plumbing
- persistence checkpoints
- FFI capability registration
- minimal lifecycle escape hatches only

## Recommended Refactor Sequence

### Phase 1: Introduce an Edict Control-Plane Module
Create a new Edict module dedicated to canonical data shaping, for example:
- `agentc_runtime_bridge.edict` or keep existing `agentc.edict` as the thin FFI bridge
- `agentc_control_plane.edict` for config/root/request helpers
- `agentc_ui_loop.edict` for interaction and presentation

The split should be:
- bridge module: raw FFI wrappers only
- control-plane module: root/config/request/response shaping
- UI module: command loop and rendering

### Phase 2: Make Root Construction Edict-Authoritative
Replace host-side `make_default_agent_root(...)` as the authoritative schema definition.

Preferred direction:
- host creates or restores a VM
- if no persisted root exists, host runs a tiny Edict bootstrap word that constructs the root from a supplied config object

This removes the duplicated root schema from C++.

### Phase 3: Collapse Request Shapes to One Canonical Internal Form
Choose one internal turn-request shape for the Edict-to-runtime boundary.

Recommended canonical shape:
- `system`
- `messages`
- optional `provider`
- optional `model`
- optional `options`
- optional `metadata`

Then:
- Edict always emits that shape
- runtime accepts that shape as the preferred contract
- `prompt` support becomes compatibility-only rather than a first-class authoring path

This reduces the number of semantic request builders.

### Phase 4: Centralize Runtime Config in Edict Data
Treat `agentc-config.json` as just one serialization form of a config object that Edict can load and manipulate directly.

Recommended rule:
- persisted `root.runtime` is the live in-VM authority
- config file is an initialization source
- host flags become bootstrap overrides only

That will remove the need for multiple independent provider/model default definitions.

### Phase 5: Move the UX Loop into Edict
Add an Edict-owned `agentc_ui_loop` that:
- waits for user input through FFI
- interprets commands
- dispatches LLM turns
- renders selected response fields
- controls streaming synchronization
- decides when to persist, reset, or request shutdown

At that point `main.cpp` becomes a lifecycle shell around an interactive VM.

## Important Non-Goals
- Do not try to eliminate provider-specific HTTP payload adapters. Those are legitimate boundary-specific differences.
- Do not move secret handling into Edict. API key lookup and native credentials should stay in C++.
- Do not collapse transport and UX into the host again; the whole point is to keep the agent state machine in the VM.

## Practical First Slice
The best first implementation slice is:
1. add a new Edict control-plane module for canonical root/config/request shaping
2. stop using C++ as the authoritative root-schema constructor
3. then migrate `main.cpp` command handling and prompt formatting into an Edict-owned UI loop

That sequence removes the most dangerous duplication first while preserving working provider code and existing persistence behavior.
