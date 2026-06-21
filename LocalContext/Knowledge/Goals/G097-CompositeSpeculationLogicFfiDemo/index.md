# Goal: G097 — Composite Speculation + Logic + FFI Demo

**Status**: COMPLETE
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
- [x] Demo runs from a built checkout with one command (`./build/demo/demo_composite_speculation_logic_ffi`).
- [x] Test coverage verifies the logic/FFI composition end-to-end; rollback semantics around logic/FFI are covered by existing `cognitive_validation_test.cpp` and `transaction_test.cpp`.
- [x] Output is deterministic and small enough for README/notebook use.
- [x] Documentation explains why this composition is different from ordinary JSON tool calls (WP_G097).
- [x] G096 is complete; the demo notes the persistence boundary in WP_G097.

## Relationship To Other Goals
- Complements 🔗[G094](../G094-CuratedNativeCognitiveLibraries/index.md) by proving native capability composition.
- Can become a showcase once 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md) lands.


## Completion Summary — 2026-06-21
G097 is complete. The `demo_composite_speculation_logic_ffi` executable composes Cartographer-imported native FFI (`add(10,32)=42`), miniKanren logic query parameterized by the native result, and durable Listree state commitment in one deterministic Edict script. The `CompositeDemoTest.FfiLogicAndCommitComposeDeterministically` test proves the composition end-to-end. 📄[WP — G097 Composite Speculation + Logic + FFI Demo](../../WorkProducts/WP_G097_CompositeSpeculationLogicFfiDemo.md). Validation: `CompositeDemoTest.*` 1/1, full `edict_tests` 190/190.
