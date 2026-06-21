# Goal: G094 — Curated Native Cognitive Capability Libraries

**Status**: COMPLETE  
**Created**: 2026-05-14  
**Completed**: 2026-06-20  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective
Provide a curated set of native capabilities that an LLM agent can import and use as cognitive operations from Edict.

## Rationale
Cartographer makes native capability import possible, but users need useful, documented capability surfaces. The summary identified three high-value libraries: structural diffing, AST parsing, and persistent knowledge-graph querying.

## Subgoals

### G094.1 — Edict Tree-Sitter Language Spec ✅
- `treesitter/edict/grammar.js` — tree-sitter grammar covering literals, identifiers, operators, sigil expressions (`@name`/`/name`/`^name` with `token.immediate`), context blocks, JSON objects, quote-words, and comments.
- Generated parser (`src/parser.c`) compiled as `libtree-sitter-edict.so` via CMake target `tree_sitter_edict`.
- Header `treesitter/edict/tree_sitter_edict.h` for C/C++ consumers.

### G094.2 — Tree-Sitter AST Bridge (C++ / Listree) ✅
- `TreeSitterBridge` and `LanguageParser` classes (`tree_sitter_bridge.h/.cpp`).
- Loads language parsers from system shared libraries via `dlopen` (`/usr/lib64/libtree-sitter-<lang>.so*`).
- Converts tree-sitter AST nodes to Listree trees with `type`, `start_byte`, `end_byte`, `text`, `start_point`, `end_point`, `children` (list), `is_named`, `is_error`, and optional `field` name.
- 15 unit tests covering C/Python/Edict parsing, byte offsets, text fields, JSON serialization, and Edict-specific grammar constructs.

### G094.3 — Edict-Level Tree-Sitter Builtins ✅
- Three VM opcodes: `VMOP_TS_LOAD`, `VMOP_TS_PARSE`, `VMOP_TS_LIST`.
- `treesitter` capsule registered in VM bootstrap (`load`/`parse`/`list`).
- 9 Edict integration tests including C parsing, Edict self-parsing, and JSON serialization.

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
- Detects Added, Removed, Modified (heuristic merge of adjacent Remove+Add with same type), and Moved (same content, different position) nodes.
- Fourth VM opcode `VMOP_TS_DIFF` with `treesitter.diff!` Edict builtin.
- Diff results convert to Listree lists for agent consumption.
- 6 structural diff unit tests + 2 Edict-level diff tests.
- Runnable example in `treesitter/examples/demo.edict`.

Edict API:
```
'c "int x;" "int x; int y;" treesitter.diff! @diff
diff to_json!   # => [{"kind":"added","type":"declaration",...}]
```

### G094.5 — Knowledge Graph ✅
- `KnowledgeGraph` class (`knowledge_graph.h/.cpp`) using Listree trees as backing store.
- Graph structure: `nodes` (tree of name → properties) + `edges` (list of {from, relation, to, properties}).
- Seven VM opcodes: `VMOP_KG_CREATE`, `KG_ADD_NODE`, `KG_ADD_EDGE`, `KG_GET_NODE`, `KG_QUERY`, `KG_LIST_NODES`, `KG_LIST_EDGES`.
- `kgraph` capsule registered in VM bootstrap (`create`/`add_node`/`add_edge`/`get_node`/`query`/`nodes`/`edges`).
- 7 knowledge graph unit tests + 3 Edict-level KG tests.
- Edge queries support wildcard matching (empty string = match all).
- Node removal cascades to connected edges.
- Data shapes are JSON-serializable and compatible with speculation/result persistence.

Edict API:
```
kgraph.create! @g
g 'main_func kgraph.add_node!
g 'a 'calls 'b kgraph.add_edge!
g 'a 'calls 'b kgraph.query! to_json!
g kgraph.nodes! to_json!
g 'main_func kgraph.get_node! to_json!
```

## Acceptance Criteria
- [x] At least one curated native cognitive library (AST parser + diff + knowledge graph) is available from Edict without bespoke per-demo glue.
- [x] Data shapes are compatible with speculation and result persistence (Listree trees, JSON-serializable).
- [x] The path is compatible with future worker sharing rules from 🔗[G092](../G092-CartographerFfiReentrancyMetadata/index.md) (bridge uses Listree, no raw pointer exposure).
- [x] Runnable example (`treesitter/examples/demo.edict`).

## Validation
- `edict_tests`: 186/186 (includes 12 TreeSitterEdictTest cases)
- `treesitter_tests`: 28/28 (15 bridge + 6 diff + 7 KG)
- `listree_tests`: 83/83
- `reflect_tests`: 55/55
- `cartographer_tests`: 52/52
- `cpp_agent_tests`: 55/55

## Notes
The tree-sitter bridge, structural diff engine, and knowledge graph are three concrete cognitive capabilities. An agent can parse code in any supported language (C, Python, Edict, etc.), navigate the AST structure, compare two source versions structurally, and build/query persistent knowledge graphs — all from Edict code without regex-based reasoning or external tools. The knowledge graph is composable with miniKanren via standard Listree tree access patterns.
