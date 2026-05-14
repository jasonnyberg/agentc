# Goal: G094 — Curated Native Cognitive Capability Libraries

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective
Provide a curated set of native capabilities that an LLM agent can import and use as cognitive operations from Edict.

## Source Extracted From
Conversation summary section **“FFI libraries an LLM agent would import”**.

## Rationale
Cartographer makes native capability import possible, but users need useful, documented capability surfaces. The summary identified three high-value libraries: structural diffing, AST parsing, and persistent knowledge-graph querying.

## Initial Capability Set
1. **Structural diff engine** — precise edit operations instead of approximate textual comparison.
2. **AST parser / tree-sitter bridge** — precise code navigation/transforms instead of regex-only reasoning.
3. **Persistent knowledge graph** — structured relationship queries, ideally composable with miniKanren.

## Implementation Plan
- [ ] Pick the first library slice and define a narrow Edict-facing API.
- [ ] Provide or import a native implementation through Cartographer/FFI.
- [ ] Wrap the capability in a stable Edict module with JSON/Listree-friendly data shapes.
- [ ] Add deterministic tests that exercise the Edict-facing API.
- [ ] Add a runnable example showing how an agent would use the capability during a task.
- [ ] Repeat for the remaining capability categories or split them into child goals when implementation starts.

## Acceptance Criteria
- [ ] At least one curated native cognitive library is available from Edict without bespoke per-demo glue.
- [ ] Documentation shows when an LLM should call the native capability instead of reasoning in prose.
- [ ] Data shapes are compatible with speculation and result persistence.
- [ ] The path is compatible with future worker sharing rules from 🔗[G092](../G092-CartographerFfiReentrancyMetadata/index.md).

## Notes
This goal is intentionally capability-oriented, not just FFI plumbing. The deliverable should feel like an agent skill/cognitive operation, not a raw C binding dump.
