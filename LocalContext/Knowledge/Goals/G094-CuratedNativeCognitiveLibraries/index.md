# Goal: G094 — Curated Native Cognitive Capability Libraries

**Status**: IN PROGRESS (AST parser / tree-sitter bridge)  
**Created**: 2026-05-14  
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective
Provide a curated set of native capabilities that an LLM agent can import and use as cognitive operations from Edict.

## Source Extracted From
Conversation summary section **"FFI libraries an LLM agent would import"**.

## Rationale
Cartographer makes native capability import possible, but users need useful, documented capability surfaces. The summary identified three high-value libraries: structural diffing, AST parsing, and persistent knowledge-graph querying.

## Initial Capability Set
1. **Structural diff engine** — precise edit operations instead of approximate textual comparison.
2. **AST parser / tree-sitter bridge** — precise code navigation/transforms instead of regex-only reasoning.
3. **Persistent knowledge graph** — structured relationship queries, ideally composable with miniKanren.

## Subgoals

### G094.1 — Edict Tree-Sitter Language Spec
**Status**: COMPLETE  
Define and build a tree-sitter grammar for the Edict language itself, so that Edict code can be parsed, navigated, and transformed by the same AST tooling used for other languages.

Deliverables:
- [x] `treesitter/edict/grammar.js` — tree-sitter grammar covering literals, identifiers, operators, sigil expressions, context blocks, JSON objects, quote-words, and comments.
- [x] Generated parser (`src/parser.c`) compiled as `libtree-sitter-edict.so`.
- [x] Header `treesitter/edict/tree_sitter_edict.h` for C/C++ consumers.
- [x] CMake target `tree_sitter_edict` builds the shared library.

Validation: grammar generates without conflicts; CLI `tree-sitter parse` produces correct ASTs for Edict source including sigil expressions (`@name`), literals (`[code]`), and JSON objects (`{"key": value}`).

### G094.2 — Tree-Sitter AST Bridge (C++ / Listree)
**Status**: COMPLETE  
C++ bridge between `libtree-sitter` and the Edict VM's Listree data model.

Deliverables:
- [x] `treesitter/tree_sitter_bridge.h` / `.cpp` — `TreeSitterBridge` and `LanguageParser` classes.
- [x] Loads language parsers from system shared libraries (`/usr/lib64/libtree-sitter-<lang>.so*`) via `dlopen`.
- [x] Parses source code and converts tree-sitter AST nodes to Listree trees with fields: `type`, `start_byte`, `end_byte`, `text`, `start_point` (row/column), `end_point`, `children` (list), `is_named`, `is_error`, and optional `field` name.
- [x] CMake target `agentc_treesitter` builds the bridge library, linked against `libtree-sitter`, `listree`, and `reflect`.
- [x] 15 unit tests in `treesitter/tests/treesitter_test.cpp` covering: language loading, C/Python/Edict parsing, byte offsets, text fields, start/end points, JSON serialization, and Edict-specific grammar constructs.

Validation: `treesitter_tests` 15/15 passed.

### G094.3 — Edict-Level Tree-Sitter Builtins
**Status**: COMPLETE  
Expose tree-sitter operations to Edict code as a `treesitter` builtin capsule.

Deliverables:
- [x] Three new VM opcodes: `VMOP_TS_LOAD`, `VMOP_TS_PARSE`, `VMOP_TS_LIST`.
- [x] `edict/edict_vm_treesitter.cpp` — opcode implementations.
- [x] `treesitter` capsule registered in VM bootstrap with `load`, `parse`, and `list` thunks.
- [x] `libedict` links against `agentc_treesitter`.
- [x] 7 Edict-level integration tests in `edict/tests/treesitter_edict_test.cpp`.

Edict API:
```
# Load a language parser (by name or library path)
'c treesitter.load!
'edict treesitter.load!

# Parse source code — pushes Listree AST
'c 'int x = 42;' treesitter.parse! @ast

# List loaded languages
treesitter.list! to_json!

# Navigate the AST via standard Listree operations
ast.type           # → "translation_unit"
ast.children       # → list of child nodes
ast.start_byte     # → "0"
ast.to_json!       # → JSON serialization
```

Validation: `TreeSitterEdictTest.*` 7/7 passed; full `edict_tests` 181/181 passed.

### G094.4 — Structural Diff Engine
**Status**: PLANNED  
Precise structural diffing using tree-sitter ASTs instead of textual comparison. Will use the AST bridge to parse two source versions and produce a structured edit list.

### G094.5 — Persistent Knowledge Graph
**Status**: PLANNED  
Structured relationship queries composable with miniKanren. May use Listree trees as the graph representation and Kanren for relational queries.

## Implementation Plan
- [x] G094.1: Edict tree-sitter grammar
- [x] G094.2: C++ tree-sitter bridge with Listree conversion
- [x] G094.3: Edict-level builtins and integration tests
- [ ] G094.4: Structural diff engine
- [ ] G094.5: Persistent knowledge graph
- [ ] Documentation: when an LLM should call the native capability instead of reasoning in prose
- [ ] Runnable example showing how an agent would use the capability during a task

## Acceptance Criteria
- [x] At least one curated native cognitive library (AST parser) is available from Edict without bespoke per-demo glue.
- [ ] Documentation shows when an LLM should call the native capability instead of reasoning in prose.
- [x] Data shapes are compatible with speculation and result persistence (Listree trees, JSON-serializable).
- [x] The path is compatible with future worker sharing rules from 🔗[G092](../G092-CartographerFfiReentrancyMetadata/index.md) (bridge uses Listree, no raw pointer exposure).

## Notes
This goal is intentionally capability-oriented, not just FFI plumbing. The deliverable should feel like an agent skill/cognitive operation, not a raw C binding dump. The tree-sitter bridge is the first concrete cognitive capability: an agent can parse code in any supported language and navigate the AST structure from Edict.
