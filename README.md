# AgentC: A Cognitive Operating Environment

AgentC is an experimental C++/Edict runtime for building agents whose working memory,
control flow, native capabilities, and constraint solving all live in one persistent,
rollback-safe substrate.

The direction is explicit: **Edict should become the agent control plane**. The current
LLM/tool-driven outer loop is useful bootstrapping machinery, but AgentC is being built to
move provider sessions, tool use, context management, speculation, and eventually delegated
worker agents into a compact VM-resident runtime. C++ remains the native substrate for
memory, persistence, credentials, transport, and unsafe capabilities; Edict owns the
cognitive protocol above it.

This repository is an internal-alpha research prototype. Many pieces are already working;
some architectural edges are still being consolidated.

---

## Why AgentC Exists

Most agent frameworks serialize cognition through text, JSON, Python, shell commands, and
stateless tool calls. That creates four recurring failures:

- **Context death**: reasoning disappears when the context window is compacted or a process
  restarts.
- **Expensive speculation**: agents reason in prose about what might happen instead of trying
  branches and rolling them back.
- **Serialization drag**: every action crosses a JSON/text boundary even when the operation is
  logically part of the same thought.
- **Externalized reasoning engines**: constraint solving, native code, and persistent memory
  are services around the agent rather than operations inside the agent.

AgentC attacks these as one problem: make cognitive state a persistent memory graph, make
branching O(1), make native functions importable into that graph, and give LLMs a compact
language designed for VM execution rather than human source files.

---

## Unique Properties

### 1. Persistent cognitive state via mmap

AgentC's core state lives in arena-backed Listree nodes. Pointers are relative offsets, so a
state image can be flushed and restored with file-backed slabs / `mmap` without rebuilding an
object graph. The long-term goal is that an agent can be killed mid-task and resumed with its
working memory intact.

### 2. O(1) speculation and rollback

Allocation is sequential. A checkpoint is a slab watermark; rollback is resetting that
watermark. The same primitive supports transactions, Edict `speculate [...]`, and miniKanren
backtracking. Branching reasoning is intended to be a cheap runtime operation, not a textual
exercise.

### 3. Token-efficient Edict language

Edict is a small concatenative language intended for LLM emission and native execution. It is
usually far more compact than equivalent Python/JSON orchestration, and isolated calls
(`f(args)`) give generated code a bounded scope so mistakes are less likely to corrupt parent
state.

### 4. FFI as a cognitive operation

Cartographer parses C/C++ headers at runtime with libclang and reflects symbols into the same
Listree substrate as agent memory. Native code is not an external tool boundary; imported
functions become Edict words that can participate in transactions, speculation, and VM state.

### 5. Embedded logic programming

miniKanren is embedded alongside Edict and Listree. Logic queries share the arena and use the
same rollback machinery as ordinary VM execution. Constraint solving can be a native cognitive
step rather than a separate service call.

**The synthesis:** speculate over an Edict block that runs a miniKanren query, calls native FFI
functions, mutates Listree state, and either commits or rolls back by resetting one integer.
Then persist the resulting cognition by flushing the memory map.

---

## Architecture at a Glance

```
LLM / user / host shell
        │
        ▼
  edict.sh curated launcher
        │
        ▼
┌─────────────────────────────────────────────────────────┐
│ Edict VM                                                │
│  • agent/provider/session control plane                 │
│  • compact stack language + contexts + transactions     │
│  • rewrite rules, speculation, miniKanren calls         │
│  • imported C/C++ functions as callable words           │
└─────────────────────────────────────────────────────────┘
        │
        ▼
┌─────────────────────────────────────────────────────────┐
│ C++ native substrate                                    │
│  • slab/Listree memory and persistence                  │
│  • Cartographer/libffi/libclang import machinery        │
│  • provider transports, credentials, streaming queues   │
│  • file/shell helper libraries and runtime C ABI        │
└─────────────────────────────────────────────────────────┘
```

### Slab + Listree

All runtime values are `ListreeValue` nodes: each node can behave as string data, a named-child
dictionary, and/or a list. Slab allocation gives fast append-style allocation and cheap
watermark rollback. Relative `CPtr<T>` pointers make the arena relocatable and persistence
friendly.

### Edict VM

Edict is the executable control surface. It has:

- stack execution and thunks (`[...]` + `!`)
- object/context mutation with `< ... >`
- transactions and `speculate [...]`
- strict/lax lookup modes
- rewrite rules
- Cursor-backed Listree traversal
- runtime FFI imports and callbacks
- first-slice thread helpers using fresh worker VMs and explicit shared cells

The VM implementation is split into core, FFI, and bootstrap translation units while keeping
the opcode dispatch loop centralized.

### Cartographer FFI

Cartographer uses libclang to parse headers and libffi to call dynamically loaded symbols.
Imported function definitions are reflected as Listree values and invoked from Edict like any
other word. The import path carries safety metadata and blocks hazardous syscall categories by
default unless explicitly allowed.

Cartographer also provides a CLI pipeline:

```text
cartographer_dump <header>
cartographer_parse <header>
  | cartographer_schema_inspect
  | cartographer_resolve <lib>
      | cartographer_resolve_inspect
```

### miniKanren

The embedded miniKanren engine supports relations including `==`, `membero`, `appendo`,
`conso`, and `conde`. Results are lazily streamed with trampoline-style execution, and
backtracking uses slab watermarks.

### C++ agent runtime

`cpp-agent/` contains the current native agent runtime layer:

- provider registry and runtime C ABI (`libagent_runtime.so`)
- local OpenAI-compatible provider support
- Google/Gemini/Gemma provider support, including live `gemma-4-31b-it` coverage
- OpenAI Codex provider support via pi ChatGPT OAuth credentials
- stream manager for background provider workers → main-thread synchronization
- session/root persistence helpers for embedded Edict agent roots

---

## Edict Quick Reference

Prefer adjacent eval spelling in examples and scripts: `word!`, `module.word!`, `thunk!!`.
The tokenizer still accepts `!` as a separate token for compatibility, but adjacent spelling is
the documented style.

```edict
['hello print] @greet
greet!                 -- prints "hello"
```

Variables and paths:

```edict
[Paris] @city
city print             -- Paris
person.address.city    -- dotted Listree lookup
```

Object/context mutation:

```edict
{} @provider
provider <
  [local-qwen] @preset_name
> pop /                -- CTX_POP returns provider; pop + / discard extra refs
```

Provider-style method calls mutate the current provider object in place:

```edict
llm.init([gemma-4-31b-it]) @provider
provider < [Reply with exactly two lowercase letters: ok] request! > pop /
provider.assistant_text print
```

Important syntax clarifications:

- Bare `/` is stack discard (`POP`).
- `/name` removes a dictionary binding.
- `/ /` discards two stack items.
- `//name` is a no-space sigil chain over `name`; bare `//` is invalid.
- `< ... >` enters a value's namespace for in-place mutation, then pushes the context object
  back when leaving.
- `provider < method! > pop /` is the common fire-and-forget mutating method pattern.

Logic query example, runnable from the project root as a script. The interactive REPL reads
one line at a time, so paste multi-line object literals through `edict -`, a file, or `-e`
rather than directly at the `>` prompt.

```bash
./build/edict/edict - <<'EDICT'
[./build/kanren/libkanren.so] [./cartographer/tests/kanren_runtime_ffi_poc.h] resolver.import! @logicffi
logicffi.agentc_logic_eval_ltv @logic

{
  "fresh": ["q"],
  "where": [["membero", "q", ["tea", "cake"]]],
  "results": ["q"]
} logic! to_json! print
EDICT
```

Expected output:

```text
["tea","cake"]
```

FFI example:

```edict
'./build/cartographer/libagentmath_poc.so './cartographer/tests/libagentmath_poc.h resolver.import! @math
'3 '4 math.hypot! print
```

---

## Edict-Resident Agent Loop

The current agent-facing surface is the curated launcher plus Edict modules under
`cpp-agent/edict/modules/`.

Key modules:

- `agentc_curated.edict` — common launcher bootstrap and module loading
- `agentc.edict` — wrappers for runtime/file/shell/stream helper libraries
- `agentc_provider_contracts.edict` — provider request/response contract specs
- `agentc_agent_root.edict` — canonical Edict-owned agent root shape
- `agentc_stateful_loop.edict` / `agentc_hello_loop.edict` — loop demos
- `llm.edict` — provider presets, `llm.init(...)`, `request`, `stream_start`, `stream_sync`,
  and provider REPL support

Launcher usage:

```bash
./edict.sh --help
EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.catalog! to_json! print'
printf 'llm.init([local-qwen]) @provider\nprovider.preset_name print\n' | ./edict.sh -
./edict.sh tests/cursor_test.edict
```

With no arguments, `edict.sh` defaults to an Edict-owned provider chat path:

```bash
./edict.sh
```

Environment knobs include `EDICT_AUTO_CHAT`, `EDICT_DEFAULT_PRESET`, `EDICT_PATH`,
`EDICT_BUILD_DIR`, `EDICT_EXT_LIB`, and `EDICT_RUNTIME_LIB`.

Provider presets currently cover local OpenAI-compatible models, Google/Gemini/Gemma paths,
and OpenAI Codex. Live Google tests check `GEMINI_API_KEY` or `GOOGLE_API_KEY` and skip when
credentials are absent.

---

## Intern / Worker-Agent Direction

AgentC is also aimed at an "intern" architecture: a primary LLM delegates bounded work to
secondary local agents without spending primary-agent context on file reading, filtering,
classification, and summarization.

Good intern tasks are narrow and checkable:

| Task type | Example |
|-----------|---------|
| Information gathering | Read files and extract matching signatures |
| Output classification | Run tests and group failures by category |
| Search + filter | Find callers of a function and discard irrelevant matches |
| Compression | Summarize a diff into semantic changes |
| Context proxy | Hold a large context and answer factual questions about it |

The intended concurrency model is:

1. Coordinator VM owns the mutable root.
2. Shared inputs/import tables are marked ReadOnly before dispatch.
3. Each intern runs in a fresh EdictVM and private slab workspace.
4. Stateless/re-entrant FFI can be shared; stateful provider handles must be cloned or kept
   coordinator-owned.
5. Workers return structured findings through a queue or copied result Listree.
6. The coordinator decides what to merge into the main cognitive state.

Existing building blocks include `LtvFlags::ReadOnly`, the pthread-backed callback/thread POC,
mutex-protected shared cells, `StreamManager`, `llm.init([local-qwen])`, and `agentc_tools`.
The full multi-intern scheduler is future work; the design target is clear, scoped delegation —
interns gather and compress, the primary agent decides.

---

## Directory Layout

```text
core/           Arena allocator, Cursor, CPtr, containers, debug helpers
listree/        Listree value graph and serialization substrate
edict/          Edict compiler, VM, REPL, builtins, tests
cartographer/   Header parser, FFI mapper/resolver, CLI pipeline, POCs
kanren/         Embedded miniKanren runtime and C ABI proof-of-concept
extensions/     Importable helper libraries / early stdlib surface
cpp-agent/      Native provider runtime, persistence, Edict modules, agent tests
demo/           Standalone demos and shell regression scripts
pi_integration/ pi/TypeScript integration experiments
LocalContext/   HRM project memory, goals, work products, and timeline
tests/          Core/listree/cursor unit tests
```

---

## Building & Running

Prerequisites:

- C++17 compiler
- CMake 3.16+ for the full tree
- `libffi-dev`
- `libclang-dev`
- `libcurl` development package
- pthreads / POSIX-like environment

Build:

```bash
cmake -B build
cmake --build build -j2
```

Raw Edict REPL:

```bash
./build/edict/edict
```

Curated AgentC launcher:

```bash
EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.catalog! to_json! print'
```

Live Google/Gemma smoke, when credentials are present:

```bash
EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.init([gemma-4-31b-it]) @provider provider < [Reply with exactly two lowercase letters: ok] request! > pop / provider.assistant_text print'
```

Optional SDL3 demo:

```bash
cmake -B build -DAGENTC_WITH_SDL_DEMO=ON -DAGENTC_SDL3_ROOT=/path/to/SDL3/install
cmake --build build
./demo/demo_sdl_triangle.sh
```

---

## Validation Status

Configured CTest suites:

```bash
ctest --test-dir build -N
```

Current focused validation baseline:

```bash
cmake --build build --target edict_tests -j2
./build/edict/edict_tests --gtest_filter='EdictVM.*:VMStackTest.*:SimpleAssignTest.*:RewriteLanguageTest.*:RegressionMatrixTest.*:CallbackTest.BootstrapImportCapsuleOwnsImportSurface:CallbackTest.ImportResolvedInjectsDefinitionsAndInvokesImmediately:CallbackTest.InterpretedImportPipelineCanRoundTripThroughJson:CallbackTest.LoadBuiltinReportsMissingLibrary:PiSimulationTest.MiniKanrenLogicExample'

cmake --build build --target cpp_agent_tests -j2
./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictAgentcModuleTest.*:EdictHelloLoopTest.*:EdictStatefulLoopTest.*:EdictAgentRootTest.*:EdictLlmModuleTest.*'
```

Recent focused runs passed the Edict style/regression slice and the cpp-agent Edict/LLM suite,
including credential-gated live Google/Gemma coverage when API keys were available.

Known caveat: the unfiltered legacy `edict_tests` binary still contains active-investigation FFI
callback/parser-map failures. Use focused filters or the HRM Dashboard for the latest validated
baseline until those are cleaned up.

---

## Current Status and Roadmap

Recently landed:

- Edict-owned provider objects via `llm.init(...)`
- curated `edict.sh` launcher and auto-chat path
- file/shell tool surface exposed through Edict wrappers
- streaming provider path using background workers and main-thread synchronization
- Google/Gemma live regression coverage (`gemma-4-31b-it`)
- OpenAI Codex provider preset
- Edict VM split into core/FFI/bootstrap implementation units
- removal of the obsolete compiler-side `Dictionary` bytecode payload path
- adjacent eval-sigil documentation/style pass (`word!`)

Immediate next work:

- explicit provider context management (`reset`, `inspect`, `trim`, summarize) in the Edict
  provider REPL path
- tighter Edict ownership of the agent root/session lifecycle
- cleanup of remaining legacy callback/parser-map test failures
- first practical intern-worker scheduler on top of fresh VMs, ReadOnly sharing, and result
  queues
- Cartographer re-entrancy annotations for safer concurrent FFI sharing

---

## Documentation

Primary references:

- [`LocalContext/Dashboard.md`](LocalContext/Dashboard.md) — current project state and latest validation baseline
- [`LocalContext/Timeline.md`](LocalContext/Timeline.md) — project history
- [`LocalContext/Knowledge/WorkProducts/edict_language_reference.md`](LocalContext/Knowledge/WorkProducts/edict_language_reference.md) — full Edict language reference
- [`LocalContext/Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md`](LocalContext/Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) — LLM-oriented Edict/VM guide
- [`LocalContext/Knowledge/WorkProducts/WP-EdictResidentAgentLoopConsolidation-2026-05-10/index.md`](LocalContext/Knowledge/WorkProducts/WP-EdictResidentAgentLoopConsolidation-2026-05-10/index.md) — Edict-resident agent loop consolidation plan
