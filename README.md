# AgentC: A Cognitive Operating Environment

## The Idea

Current agentic AI systems suffer from an impedance mismatch: they force LLMs to reason in
human-centric languages — Python, JSON, shell — that are token-inefficient, fragile, and
computationally opaque. The result is context window exhaustion, fragile error recovery, and
reasoning that disappears between turns.

AgentC is a C++ substrate designed from first principles to address this. It provides a
reversible, arena-backed runtime where all state is a traversable tree, all allocations are
O(1) undoable, native C/C++ capabilities are dynamically reflected into that tree, and
relational constraint queries are a first-class operation. The goal is not to replace the
outer tool-driven agent loop, but to give it a stronger cognitive foundation: structured
memory, speculative execution, and logic reasoning that survive across turns.

---

## Architecture

### 1. Slab & Listree

The foundation is a **slab-allocated arena**. All runtime state — values, variables, code
frames, logic bindings — lives in a contiguous memory region managed by a slab allocator.

- **`CPtr<T>`** — pointers are relative offsets, not absolute addresses. The arena can be
  serialized with a raw `memcpy` and restored with `mmap`. No pointer swizzling.
- **O(1) rollback** — allocation is sequential. Saving the watermark (an integer) is a
  checkpoint; resetting it is an instant rollback of all state created since. This is the
  mechanism behind speculative execution, logic backtracking, and transactions.
- **Listree** — every runtime value is a `ListreeValue` node: simultaneously a string, a
  named-child dictionary, and a list. There is no separate type for stack frames, scopes,
  or data structures — everything is the same node type, arena-allocated.
- **String storage** uses three tiers: inline SSO (≤ 15 bytes, no allocation), slab blob
  (larger strings, arena-owned), and static view (zero-copy pointer into external memory).

### 2. Edict Language

Edict is a **stack-based concatenative language** — tokens either push values or perform
operations. Programs are sequences; results accumulate on the data stack.

```edict
"hello" "world" print   -- pushes two strings, prints "world"
```

**Thunks and eval.** Square brackets push their contents as a literal string (a thunk). `!`
pops and executes whatever is on top — a thunk, a built-in, or an FFI function. If `!` is
the last instruction in the current frame, it tail-calls.

```edict
["hello" print] @greet
greet !                 -- executes the thunk; prints "hello"
```

**Variables and dotted paths.** `@name` stores, bare `name` looks up. Paths like
`player.position.x` resolve through the Listree tree via the Cursor. Unknown names push
themselves as strings (soft failure, no exceptions).

**Conditionals.** The `test`/`&`/`|` idiom replaces bytecode jumps with a state-stack
pattern. This composes cleanly and supports nesting:

```edict
value test & [do_this !] | [do_that !]
```

**Speculative execution.** `speculate` runs a code block in a full VM snapshot. On
success, the result is returned; on failure, null. The host VM is never mutated:

```edict
"default"
speculate [risky_op !]
dup test & [swap /] | [/]    -- use result if non-null, else keep default
```

**Logic queries.** An embedded miniKanren engine supports relational constraint solving.
Backtracking uses Slab watermark reset — no copying:

```edict
logic {
  "fresh":   ["head", "tail"],
  "where":   [["conso", "head", "tail", ["tea", "cake"]]],
  "results": ["head", "tail"]
}
-- stack: [ [["tea", ["cake"]]] ]
```

**FFI.** Native C/C++ libraries are imported at runtime. After import, functions are called
like any other word:

```edict
"./libmath.so" "./math.h" "math" resolver.import ! @math
"3" "4" math.hypot !    -- stack: [ "5" ]
```

Rewrite rules, transactions, cursor navigation, FFI closures, and the full opcode set are
documented in the language reference (see Documentation).

### 3. Cartographer

The Cartographer uses **`libclang`** to parse C/C++ headers and **`libffi`** to construct
call frames dynamically. It translates function signatures and struct layouts directly into
Listree definitions in the shared Arena. The agent sees new words immediately, with no
static bindings or generated glue code.

Imported functions carry safety metadata. A syscall blocklist (`PROC/FILE/NET/DL/MEM/SIG`)
blocks hazardous operations by default; `unsafe_extensions_allow` opts in explicitly.

### 4. Mini-Kanren

An embedded logic engine for relational constraint queries. Supported relations include
unification (`==`), `membero`, `appendo`, `conso`, and `conde` disjunction. Results stream
lazily via `snooze()`-based trampolining, which eliminates stack overflow on infinite
streams. Backtracking is O(1) via Slab watermark reset.

Mini-Kanren is a strong fit for bounded agent problems: dependency conflict resolution,
migration ordering, API signature matching, and determining the minimal change set
satisfying a set of constraints.

---

## Directory Layout

```
core/          Arena allocator, Listree, Cursor, CPtr, container types (CLL, AATree)
edict/         Edict VM, compiler, REPL, tests
cartographer/  Cartographer service, FFI mapper, resolver
kanren/        Mini-Kanren engine
listree/       Listree library and tests
tests/         Unit tests for core/ components
demo/          Standalone demo programs
```

---

## Building & Running

**Prerequisites:** C++17 compiler, CMake 3.10+, `libffi-dev`, `libclang-dev`

```bash
cmake -B build
cmake --build build
```

**REPL:**

```bash
./build/edict/edict      # or: make repl
```

**Tests:**

```bash
ctest --test-dir build
```

All 74 tests across 7 suites pass in under 3 seconds.

---

## Status

The core runtime is complete and stable:

- Arena allocator with checkpoint/rollback, three ArenaStore backends (memory, file, LMDB via `dlopen`)
- Edict VM with full transaction support, speculative execution, rewrite rules, and miniKanren
- Cartographer dynamic FFI with safety tagging and async import
- Cursor-based Listree traversal with glob, iterator, and tail-deref modes
- All namespaces under `agentc::` (`agentc::edict`, `agentc::cartographer`, `agentc::kanren`)

Next: **LMDB persistent arena** (Goals G040–G042) — structured save/restore of full VM root
state, enabling resumable agent sessions.

---

## Looking Ahead

AgentC's most compelling near-term role is as an **internal cognitive substrate** behind an
interactive agent. The outer agent handles user interaction, tool calls, file edits, and
shell orchestration. AgentC handles what current agents do poorly: reversible speculative
state, compact structured intermediate memory, relational constraint solving, and persistent
cognitive scratchpads.

Concrete directions:

- **Persistent sessions** — serialize and restore full VM state via LMDB, enabling agents
  to resume mid-task across process boundaries.
- **Zero-copy sub-agent handoff** — spawn a sub-agent by passing the Arena's integer offset;
  it wakes inside the full shared context without serialization.
- **Logic coprocessor** — route bounded planning problems (dependency solving, failure
  triage, migration sequencing) to miniKanren rather than open-ended LLM inference.
- **Native capability layer** — use Cartographer/FFI as a typed adapter for performance-
  sensitive native devtools (build graph analyzers, symbol inspectors, parser adapters).

---

## Documentation

- [`AgentLang.md`](AgentLang.md) — design philosophy, architectural vision, and integration
  roadmap
- [`doc/AgentC.md`](doc/AgentC.md) — technical foundation
- [`LocalContext/Knowledge/WorkProducts/edict_language_reference.md`](LocalContext/Knowledge/WorkProducts/edict_language_reference.md)
  — full Edict language reference with runnable examples
