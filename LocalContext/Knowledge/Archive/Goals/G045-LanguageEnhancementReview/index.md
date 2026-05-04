# G045 - Language Enhancement Review for Human + Agent Workflows

## Status: COMPLETE (2026-03-22)

## Goal

Review the AgentC/Edict codebase and project documents to infer the system's intent as a programming language and runtime useful to both humans and agents, then produce a forward-looking design document with novel ideas that could simplify the implementation and enrich the language's best-fit feature set.

## Outcome

- Completed a code-and-doc review across `README.md`, the Edict language reference, representative VM/runtime code, Cartographer service code, and cognitive/logic tests.
- Produced work product 🔗[`AgentCLanguageEnhancements-2026-03-22.md`](../../WorkProducts/AgentCLanguageEnhancements-2026-03-22.md).

## Key Findings

1. AgentC is strongest as a **cognitive substrate**, not a Python replacement: reversible state, compact structured memory, dynamic FFI reflection, and bounded logic search are the core differentiators.
2. The implementation already trends toward simplification by collapsing special cases: numeric semantics moved out to FFI, boxing opcodes were removed, import metadata was simplified, and resolved JSON became a cache boundary.
3. The main opportunities are to make the substrate more explicit and composable without widening the VM too much: continuation-based speculation, better transaction deltas, stricter identifier modes, stronger explainability for the unified literal model, richer capability metadata, and better inspection/tooling.

## Sources Reviewed

- `README.md`
- `LocalContext/Knowledge/WorkProducts/edict_language_reference.md`
- `listree/listree.h`
- `edict/edict_vm.cpp`
- `cartographer/service.cpp`
- `edict/tests/cognitive_validation_test.cpp`
- `edict/tests/logic_surface_test.cpp`
- `demo/demo_cognitive_core.sh`

## Related Context

- 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- 🔗[`LocalContext/Knowledge/Timeline/2026/03/22/index.md`](../../../Knowledge/Timeline/2026/03/22/index.md)
