# Goal: G094 — Curated Native Cognitive Capability Libraries

**Status**: IN PROGRESS (AST bridge + structural diff complete)  
**Created**: 2026-05-14  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective
Provide a curated set of native capabilities that an LLM agent can import and use as cognitive operations from Edict.

## Rationale
Cartographer makes native capability import possible, but users need useful, documented capability surfaces. The summary identified three high-value libraries: structural diffing, AST parsing, and persistent knowledge-graph querying.

## Subgoals

### G094.1 — Edict Tree-Sitter Language Spec ✅
- `treesitter/edict/grammar.js` — tree-sitter grammar covering literals, identifiers, operators, sigil expressions, context blocks, JSON objects, quote-words, and comments.
- Generated parser (`src/parser.c`) compiled as `libtree-sitter-edict.so`.
- Header `treesitter/edict/tree_sitter_edict.h` for C/C++ consumers.
- CMake target `tree_sitter_edict` builds the shared library.

### G094.2 — Tree-Sitter AST Bridge (C++ / Listree) ✅
- `TreeSitterBridge` and `LanguageParser` classes (`tree_sitter_bridge.h/.cpp`).
- Loads language parsers from system shared libraries via `dlopen`.
- Converts tree-sitter AST nodes to Listree trees with `type`, `start_byte`, `end_byte`, `text`, `start_point`, `end_point`, `children` (list), `is_named`, `is_error`, and optional `field` name.
- 15 unit tests in `treesitter/tests/treesitter_test.cpp`.

### G094.3 — Edict-Level Tree-Sitter Builtins ✅
- Three VM opcodes: `VMOP_TS_LOAD`, `VMOP_TS_PARSE`, `VMOP_TS_LIST`.
- `treesitter` capsule registered in VM bootstrap (`load`/`parse`/`list`).
- 9 Edict integration tests in `edict/tests/treesitter_edict_test.cpp`.

Edict API:
```
'c treesitter.load!
'c "int x;" treesitter.parse! @ast
ast.type            # => "translation_unit"
ast.children        # => list of child nodes
treesitter.list! to_json!
```

### G094.4 — Structural Diff Engine ✅
- `StructuralDiff` class (`structural_diff.h/.cpp`) — LCS-based AST comparison.
- Detects Added, Removed, Modified, and Moved nodes.
- Fourth VM opcode `VMOP_TS_DIFF` with `treesitter.diff!` Edict builtin.
- Diff results convert to Listree lists for agent consumption.
- 6 structural diff unit tests + 2 Edict-level diff tests.
- Runnable example in `treesitter/examples/demo.edict`.

Edict API:
```
'c "int x;" "int x; int y;" treesitter.diff! @diff
diff to_json!   # => [{"kind":"added","type":"declaration",...}]
```

### G094.5 — Persistent Knowledge Graph
**Status**: PLANNED  
Structured relationship queries composable with miniKanren. Will use Listree trees as the graph representation and Kanren for relational queries. The graph will persist via the existing slab/mmap infrastructure.

## Implementation Plan
- [x] G094.1: Edict tree-sitter grammar
- [x] G094.2: C++ tree-sitter bridge with Listree conversion
- [x] G094.3: Edict-level builtins and integration tests
- [x] G094.4: Structural diff engine with LCS-based AST comparison
- [ ] G094.5: Persistent knowledge graph
- [x] Runnable example (`treesitter/examples/demo.edict`)
- [ ] Documentation: when an LLM should call the native capability instead of reasoning in prose

## Acceptance Criteria
- [x] At least one curated native cognitive library (AST parser + diff) is available from Edict without bespoke per-demo glue.
- [ ] Documentation shows when an LLM should call the native capability instead of reasoning in prose.
- [x] Data shapes are compatible with speculation and result persistence (Listree trees, JSON-serializable).
- [x] The path is compatible with future worker sharing rules from 🔗[G092](../G092-CartographerFfiReentrancyMetadata/index.md) (bridge uses Listree, no raw pointer exposure).

## Notes
The tree-sitter bridge and structural diff engine are the first two concrete cognitive capabilities. An agent can parse code in any supported language (C, Python, Edict, etc.), navigate the AST structure, and compare two source versions structurally — all from Edict code without regex-based reasoning.
