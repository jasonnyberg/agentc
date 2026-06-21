# Goal: G095 — Edict Cognitive Skill Scaffolds

**Status**: COMPLETE
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
- [x] Define stable Listree/JSON shapes for hypotheses, findings, concerns, and refactor steps.
- [x] Implement the first narrow scaffold as an Edict module (`cognitive.edict`).
- [x] Add deterministic tests around state transitions and snapshot/restore isolation.
- [x] Add examples that an LLM can copy directly (documented in WP_G095).
- [x] miniKanren integration documented as future work for refactor-plan dependency validation.

## Acceptance Criteria
- [x] At least one scaffold is usable from Edict and covered by tests.
- [x] Scaffold state survives ordinary provider turns and can be serialized/printed for inspection.
- [x] Speculative branches do not corrupt parent task state (JSON snapshot/restore isolation proven in tests).
- [x] Documentation explains success criteria and when to use each scaffold (WP_G095).

## Relationship To Other Goals
- Builds on the Edict-owned loop from 🔗[G078](../G078-EdictResidentAgentLoopConsolidation/index.md).
- Can use curated native capabilities from 🔗[G094](../G094-CuratedNativeCognitiveLibraries/index.md).
- Can later become intern-task templates for 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md).


## Completion Summary — 2026-06-21
G095 is complete. The `cognitive.edict` module provides reusable investigation, code-review, and refactor-plan scaffold constructors with stable Listree/JSON shapes. The investigation scaffold has full state-transition words (`add_hypothesis`, `add_evidence`, `mark`) and is covered by 3 deterministic tests proving persistence/serialization across VM executes, snapshot/restore branch isolation, and companion shape inspectability. 📄[WP — G095 Edict Cognitive Skill Scaffolds](../../WorkProducts/WP_G095_EdictCognitiveSkillScaffolds.md). Validation: `CognitiveScaffoldTest.*` 3/3, full `edict_tests` 190/190.
