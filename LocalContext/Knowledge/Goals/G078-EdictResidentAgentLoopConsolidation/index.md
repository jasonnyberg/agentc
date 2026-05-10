# Goal: G078 — Edict-Resident Agent Loop Consolidation

**Status**: IN PROGRESS  
**Created**: 2026-05-10

## Objective
Consolidate AgentC's duplicated request-shaping, config, root-schema, and UX-loop logic so the Edict VM becomes the authoritative control plane for the user-facing agent experience.

## Rationale
The current system works, but semantic pieces of an LLM turn are duplicated across shell demos, Edict modules, host bootstrap code, runtime normalization, and provider adapters. That duplication makes it harder to evolve the agent loop cleanly and increases the odds of drift between demos, production, and providers.

## Acceptance Criteria
- [ ] Define one canonical Edict-resident schema for agent root, runtime config, and normalized turn requests.
- [ ] Reduce the runtime C ABI boundary to a thinner execution/transport layer rather than a secondary request-authoring layer.
- [ ] Move the interactive UX loop out of `cpp-agent/main.cpp` and into Edict, leaving the host responsible only for lifecycle, persistence, attach/detach transport, and FFI exposure.
- [ ] Eliminate redundant host-side/root-side default model/provider/system-prompt definitions where Edict can be the single authority.
- [ ] Preserve provider-specific wire adapters in C++ while removing semantic duplication above them.

## Current Findings
- Demo scripts currently duplicate bootstrap imports plus provider/model/system-prompt setup.
- `agentc_stateful_loop.edict` and `agentc_agent_root.edict` already demonstrate that Edict can own request assembly and turn-state mutation.
- `cpp-agent/main.cpp` still owns the live UI loop, command handling, and fallback root/config construction.
- `cpp-agent/runtime/core/runtime.cpp` still performs request normalization and fallback resolution that overlaps with the desired Edict control-plane role.
- `cpp-agent/runtime/persistence/agent_root_vm_ops.cpp` still duplicates the root schema in `make_default_agent_root(...)` relative to the Edict root constructors.
- The first concrete provider-contract slice is now live: `cpp-agent/edict/modules/agentc_provider_contracts.edict` carries contrasting `local` and `google` provider semantics, and `agentc_agent_root.edict` now builds canonical turn requests from `runtime.provider_contract` instead of relying on the older generic state-turn helper.
- Embedded-VM bootstrap and restore currently mirror those provider contracts in C++ (`provider_contract_json_for(...)`) as a temporary bridge so host-created and restored roots can participate in the new Edict-native request-building path without dynamic provider lookup inside Edict.
- The initial attempt to resolve provider contracts dynamically in pure Edict via string equality was invalid because the current Edict surface does not support `==` for this use; explicit `provider_contract` carriage is the working near-term seam.
- The first slice is regression-covered across direct Edict root tests, embedded runtime turns, restore/resume, and session persistence, so the new contract seam is now safe to extend.

## Primary Direction
Make Edict the source of truth for:
- agent root schema
- runtime config object shape
- normalized request construction
- UX loop state machine
- response projection for display

Keep C++ authoritative only for:
- provider transport
- credentials and native secrets lookup
- provider-specific payload translation
- persistence and process lifecycle
- socket attach/detach transport

## Progress Notes
- 2026-05-10: Landed the first Edict-side provider contract module (`agentc_provider_contracts.edict`) with `local` and `google` as contrasting shapes.
- 2026-05-10: Refactored `agentc_agent_root_turn` so the root module stages the user turn, builds the canonical request from `runtime.provider_contract`, calls the runtime, and reapplies the response into root-owned conversation state.
- 2026-05-10: Updated embedded-VM bootstrap/import artifacts and focused regression suites so the provider-contract seam is exercised through root construction, runtime invocation, persistence, and restore.
- 2026-05-10: Remaining consolidation debt after this slice is primarily the temporary C++ contract mirroring during bootstrap/rehydration and the still-host-owned UX loop in `cpp-agent/main.cpp`.
