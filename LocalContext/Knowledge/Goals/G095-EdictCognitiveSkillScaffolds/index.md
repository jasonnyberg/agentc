# Goal: G095 — Edict Cognitive Skill Scaffolds

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective
Create reusable Edict-level cognitive scaffolds for common agent workflows: investigation, code review, and multi-file refactoring planning.

## Source Extracted From
Conversation summary section **“Cognitive skills an LLM agent would curate”**.

## Rationale
AgentC's durable memory, speculation, and logic substrate are most valuable when packaged into repeatable task patterns that agents can invoke. These scaffolds should help models maintain structured task state instead of relying only on transcript memory.

## Initial Skill Set
- **Investigation scaffold**: hypothesis tree management with `speculate [...]` per branch.
- **Code review state machine**: persistent tracking of concerns, evidence, and verification status.
- **Multi-file refactoring plan**: dependency-aware plan state with checkpoint/rollback per file.

## Implementation Plan
- [ ] Define stable Listree/JSON shapes for hypotheses, findings, concerns, and refactor steps.
- [ ] Implement the first narrow scaffold as an Edict module or demo script.
- [ ] Add deterministic tests around state transitions and rollback behavior.
- [ ] Add examples that an LLM can copy directly.
- [ ] Integrate miniKanren where it naturally helps with dependency/order constraints.

## Acceptance Criteria
- [ ] At least one scaffold is usable from Edict and covered by tests.
- [ ] Scaffold state survives ordinary provider turns and can be serialized/printed for inspection.
- [ ] Speculative branches do not corrupt parent task state.
- [ ] Documentation explains success criteria and when to use each scaffold.

## Relationship To Other Goals
- Builds on the Edict-owned loop from 🔗[G078](../G078-EdictResidentAgentLoopConsolidation/index.md).
- Can use curated native capabilities from 🔗[G094](../G094-CuratedNativeCognitiveLibraries/index.md).
- Can later become intern-task templates for 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md).
