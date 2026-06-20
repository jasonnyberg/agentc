# Goal: G101 — Direct Edict Tool-Emission Path

**Status**: COMPLETE
**Created**: 2026-05-14
**Completed**: 2026-06-20
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective

Make direct Edict emission a practical tool-use path for LLM agents, reducing JSON serialize/deserialize round trips where the model can safely emit compact Edict programs.

## Source Extracted From

Conversation summary section **“Immediate high-value capabilities”**: direct Edict emission as an alternative to JSON tool-call round trips.

## Rationale

One of AgentC's proposed advantages is that an LLM can emit compact VM-native actions instead of asking an outer host to interpret JSON tool calls. This needs a safe, inspectable execution path with clear boundaries and examples.

## Implementation Summary

G101 now provides the first guarded direct-action dispatcher:

- `agentc_direct_action!` accepts a compact Edict action object.
- The MVP whitelist permits `{"op":"read_file","path":"..."}` only.
- The allowed path dispatches to the existing `agentc_file_read!` helper and annotates the envelope with `emission: "direct-edict-v1"` and `op: "read_file"`.
- Unsafe or unknown operations return an inspectable denial envelope with `error.code: "direct_action_denied"` rather than executing.
- `edict.sh` now imports `ext` and `runtimeffi` in the curated prelude so direct snippets can use `agentc.edict` wrappers outside a provider object.

WorkProduct: 🔗[WP — G101 Direct Edict Tool-Emission Path](../../WorkProducts/WP_G101_DirectEdictToolEmissionPath.md)

## Implementation Plan

- [x] Define the allowed direct-Edict action subset for model-emitted code.
- [x] Add guardrails around capability access, unresolved symbols, failure state, and context mutation.
- [x] Provide examples where direct Edict replaces JSON tool-call glue for file/shell/provider operations.
- [x] Add validation comparing direct Edict execution against equivalent JSON-envelope helper behavior.

## Acceptance Criteria

- [x] A model-emitted Edict snippet can invoke at least one safe tool path through the curated launcher.
  - Evidence: `EdictAgentcModuleTest.DirectEdictActionReadsFileThroughCuratedLauncher` runs via `edict.sh -` and invokes `agentc_direct_action!` for `read_file`.
- [x] Unsafe/native capability access is gated or excluded from the direct-emission subset.
  - Evidence: `EdictAgentcModuleTest.DirectEdictActionRejectsUnsafeShellOperation` verifies `shell` returns `direct_action_denied` data instead of executing.
- [x] Errors are inspectable by the agent without corrupting provider/session state.
  - Evidence: denied operations return an ordinary JSON-serializable Edict envelope; read failures continue to use the existing `agentc_file_read!` error envelope; direct actions do not touch provider conversation state.
- [x] Documentation explains when JSON envelopes remain preferable.
  - Evidence: WP_G101 documents the direct-action contract, denied operations, launcher boundary, and JSON-envelope preference cases.

## Validation

- `cmake --build build --target cpp_agent_tests -j$(nproc)` — passed.
- `./cpp-agent/cpp_agent_tests --gtest_filter='EdictAgentcModuleTest.DirectEdictAction*'` — passed 2/2.
- `./cpp-agent/cpp_agent_tests --gtest_filter='EdictAgentcModuleTest.*'` — passed 6/6.
- `./cpp-agent/cpp_agent_tests --gtest_filter='EdictLlmModuleTest.*'` — passed 11/11.
- `./cpp-agent/cpp_agent_tests` — passed 55/55.

## Relationship To Other Goals

- Depends on stable tool/provider surfaces from 🔗[G078](../G078-EdictResidentAgentLoopConsolidation/index.md) and 🔗[G079](../G079-EdictAgentLoopToolSupport/index.md).
- Complements 🔗[G100](../G100-EdictIsolationContractHardening/index.md), which remains responsible for broader arbitrary emitted-source sandboxing and isolation hardening.
