# Goal: G101 — Direct Edict Tool-Emission Path

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective
Make direct Edict emission a practical tool-use path for LLM agents, reducing JSON serialize/deserialize round trips where the model can safely emit compact Edict programs.

## Source Extracted From
Conversation summary section **“Immediate high-value capabilities”**: direct Edict emission as an alternative to JSON tool-call round trips.

## Rationale
One of AgentC's proposed advantages is that an LLM can emit compact VM-native actions instead of asking an outer host to interpret JSON tool calls. This needs a safe, inspectable execution path with clear boundaries and examples.

## Implementation Plan
- [ ] Define the allowed direct-Edict action subset for model-emitted code.
- [ ] Add guardrails around capability access, unresolved symbols, failure state, and context mutation.
- [ ] Provide examples where direct Edict replaces JSON tool-call glue for file/shell/provider operations.
- [ ] Add validation comparing direct Edict execution against equivalent JSON-envelope helper behavior.

## Acceptance Criteria
- [ ] A model-emitted Edict snippet can invoke at least one safe tool path through the curated launcher.
- [ ] Unsafe/native capability access is gated or excluded from the direct-emission subset.
- [ ] Errors are inspectable by the agent without corrupting provider/session state.
- [ ] Documentation explains when JSON envelopes remain preferable.

## Relationship To Other Goals
- Depends on stable tool/provider surfaces from 🔗[G078](../G078-EdictResidentAgentLoopConsolidation/index.md) and 🔗[G079](../G079-EdictAgentLoopToolSupport/index.md).
- Complements 🔗[G100](../G100-EdictIsolationContractHardening/index.md).
