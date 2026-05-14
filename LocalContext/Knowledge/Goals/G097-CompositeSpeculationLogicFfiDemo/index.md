# Goal: G097 — Composite Speculation + Logic + FFI Demo

**Status**: PLANNED  
**Created**: 2026-05-14

## Objective
Build a runnable demonstration that composes AgentC's distinctive primitives: `speculate [...]`, miniKanren, Cartographer-imported FFI, and persistent Listree state.

## Source Extracted From
Conversation summary synthesis: **“Speculate over a logic query that calls native FFI functions, with the entire execution rollback-safe via a single integer reset, and the result persisted by flushing a memory map.”**

## Rationale
AgentC's strongest claim is the composition of persistence, rollback, FFI, and logic in one substrate. A compact, tested demo would make that claim concrete for users and provide regression coverage for the seams between these systems.

## Candidate Demo Shape
- Import a small deterministic native library through Cartographer.
- Run a miniKanren query whose constraints call or are parameterized by imported native behavior.
- Execute the query inside `speculate [...]` and demonstrate parent-state rollback.
- Commit selected results into durable Listree state.
- If 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md) is not complete, use the current best available persistence/serialization path and mark the mmap flush as a future extension.

## Acceptance Criteria
- [ ] Demo runs from a built checkout with one command.
- [ ] Test coverage verifies rollback semantics around the logic/FFI composition.
- [ ] Output is deterministic and small enough for README/notebook use.
- [ ] Documentation explains why this composition is different from ordinary JSON tool calls.
- [ ] If full mmap resume is unavailable, the demo clearly distinguishes current behavior from the long-term persistence target.

## Relationship To Other Goals
- Complements 🔗[G094](../G094-CuratedNativeCognitiveLibraries/index.md) by proving native capability composition.
- Can become a showcase once 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md) lands.
