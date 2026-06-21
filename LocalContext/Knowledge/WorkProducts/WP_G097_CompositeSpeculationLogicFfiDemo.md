# WP — G097 Composite Speculation + Logic + FFI Demo

## Purpose

G097 demonstrates AgentC's distinctive composition in one small runnable artifact:

1. Cartographer imports a deterministic native math helper (`libagentmath_poc.so`).
2. Cartographer imports the miniKanren evaluator (`libkanren.so`).
3. Edict calls native `add(10, 32)` to produce `42`.
4. Edict uses that native result to parameterize a miniKanren query.
5. A transient branch-state mutation and the logic+FFI query run inside a C++ transaction checkpoint.
6. The transaction is rolled back, parent state restores to baseline, and selected answers are committed into ordinary Listree state.

## Runnable Demo

Built target:

```bash
cmake --build build --target demo_composite_speculation_logic_ffi -j2
./build/demo/demo_composite_speculation_logic_ffi
```

Expected deterministic output shape:

```text
Composite Speculation + Logic + FFI demo
native_sum=42
parent_after_speculate=baseline
probe_answers=[42]
committed_answers=[42]
```

## Why This Is Different From JSON Tool Calls

Ordinary JSON tool calls usually serialize a request to a tool, receive a response, and rely on the outer agent loop to remember or merge the result. This demo keeps the composition inside Edict/Listree:

- Native capabilities are imported as callable Edict values through Cartographer.
- The miniKanren query is an ordinary Listree object, not a separate language runtime hidden from Edict.
- The C++ transaction API (`beginTransaction`/`rollbackTransaction`) provides the same rollback substrate that backs Edict-level `speculate`, and is used here to wrap the imported logic/FFI call with state mutation.
- The selected result is committed by assigning into Listree state after rollback, making the durable boundary explicit.

## Persistence Boundary

G096 is complete, so ordinary named-session Listree state can be persisted by the existing session-image path. This demo itself is intentionally a compact built-checkout executable, not a long-running named-session script; it demonstrates the durable-state shape by committing selected answers to `demo_state`. Full notebook-style persistence walkthroughs remain future documentation work.

## Verification Matrix

| Test / Command | Coverage |
|---|---|
| `CompositeDemoTest.SpeculationLogicAndFfiComposeWithDeterministicCommit` | imports math + miniKanren through Cartographer, uses native `add` result in logic query, proves speculative parent rollback, commits deterministic answer |
| `./build/demo/demo_composite_speculation_logic_ffi` | one-command built-checkout demo with small deterministic output |

## Future Work

- Add an `edict.sh` script variant once curated module loading grows a first-class resolved-import helper.
- Add a README/notebook cell after G098 packages the architecture vision work product.
