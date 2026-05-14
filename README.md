# AgentC: Persistent Cognition for AI Agents

AgentC is an experimental runtime for agents that need more than prompt text, JSON tool
calls, and short-lived scratchpads. It gives an agent a persistent memory graph, a compact
native language ("Edict", for Executable Dictionary), rollback-safe execution, dynamic
access to C/C++ capabilities, and embedded logic programming in one substrate.

Edict, AgentC's VM language, becomes the
agent control plane. Provider sessions, tool use, context management, speculative branches,
and delegated worker agents should move into the VM. C++ remains the native layer for memory,
persistence, credentials, transports, and unsafe system capabilities.

AgentC is currently an internal-alpha research prototype. It is useful for experimentation and
for building early agent loops, but interfaces are still evolving.

---

## Why a User Might Want AgentC

Most agent systems serialize cognition through text and API calls. That works for simple tool
use, but it becomes expensive when an agent needs durable state, branching reasoning, native
capabilities, or local helper agents.

AgentC is designed for applications where an agent should be able to:

- **Keep working memory alive across turns and process boundaries** instead of reconstructing
  state from summaries.
- **Try a branch and roll it back cheaply** instead of reasoning only in prose about what might
  happen.
- **Call native libraries as part of cognition** instead of treating every capability as a
  separate JSON tool boundary.
- **Use logic queries for bounded planning problems** such as dependency resolution, migration
  ordering, or API-shape matching.
- **Delegate narrow work to local worker agents** without spending the primary model's context
  window on file reading, filtering, or summarization.

AgentC is not just another chat wrapper. It is a candidate operating environment for agents
whose memory, actions, constraints, and provider state should live together.

---

## What Makes AgentC Different

### Persistent cognitive memory

AgentC stores state in arena-backed Listree nodes. Pointers are relative offsets, so state can
be persisted through file-backed slabs and restored without reconstructing an object graph. The
long-term goal is simple: kill an agent mid-task, restart it, and continue from the same
cognitive state.

### O(1) speculative execution

AgentC allocation is watermark-based. A checkpoint is one integer; rollback is resetting that
integer. The same primitive supports Edict `speculate [...]`, transactions, and miniKanren
backtracking. Branch exploration becomes a runtime operation instead of an expensive text-only
habit.

### A compact language for agent control

Edict is a small concatenative language built for VM execution and LLM emission. It is compact,
stack-oriented, and designed around explicit state effects. This makes it suitable for provider
loops, tool orchestration, rollback probes, and generated helper code.

### Native capabilities without glue code

Cartographer imports C/C++ headers at runtime using libclang/libffi and reflects functions into
AgentC memory. Imported functions become Edict-callable words. A structural diff engine, AST
parser, solver, database binding, or specialized system library can become part of the agent's
working environment.

### Logic programming inside the agent

miniKanren is embedded alongside Edict. Logic queries share the same memory substrate and use
the same rollback mechanics. Constraint solving can be part of the agent's thought process, not
an external microservice.

---

## Applications AgentC Is Aimed At

| Application | Why AgentC helps |
|-------------|------------------|
| Persistent coding agents | Durable scratch memory, provider state, and project facts survive context churn. |
| Refactoring planners | Speculative branches and logic constraints can model alternative edit plans. |
| Tool-rich local agents | Native libraries can be imported directly instead of wrapped by ad-hoc services. |
| Long-running research agents | Intermediate findings can live in structured memory, not only in transcript text. |
| Local intern agents | Secondary models can gather, filter, and summarize while the primary model decides. |
| Constraint-heavy automation | miniKanren can solve bounded dependency/order/matching problems explicitly. |

---

## Mental Model

AgentC has four user-visible layers:

```text
Agent / user intent
        │
        ▼
Edict control plane
  provider loops, tools, memory updates, speculation, logic calls
        │
        ▼
Persistent Listree state
  one graph for objects, scopes, lists, runtime data, imported capabilities
        │
        ▼
Native substrate
  mmap/slabs, C/C++ FFI, provider transports, credentials, file/shell helpers
```

The important shift is that the agent is not just asking a host process to run tools. The agent
can increasingly *own the loop* inside Edict: maintain provider state, mutate structured memory,
call native code, probe branches, and decide what to keep.

---

## Quick Start

Prerequisites:

- C++17 compiler
- CMake 3.16+
- `libffi-dev`
- `libclang-dev`
- `libcurl` development package
- POSIX-like environment with pthreads

Build:

```bash
cmake -B build
cmake --build build -j2
```

List available LLM/provider presets through the curated launcher:

```bash
EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.catalog! to_json! print'
```

Start the raw Edict REPL:

```bash
./build/edict/edict
```

Create or resume a named Edict session. Session images are stored under
`/tmp/session/<id>/` by default; use `--session-base` or `EDICT_SESSION_BASE` to choose a
different root.

```bash
./build/edict/edict --session demo -e "'persisted @answer"
./build/edict/edict --session demo -e 'answer print'
```

Start the curated AgentC launcher. With the default environment it enters the Edict-owned
provider chat path:

```bash
./edict.sh
```

Use `EDICT_AUTO_CHAT=0` when you want raw curated Edict execution instead of chat mode.

---

## Use Patterns

### 1. Provider object controlled from Edict

The current LLM surface centers on stable provider objects. A provider owns runtime config,
conversation state, request thunks, streaming thunks, and tool references.

```edict
llm.init([gemma-4-31b-it]) @provider
provider < [Reply with exactly two lowercase letters: ok] request! > / /
provider.assistant_text print
```

The pattern `provider < method! > / /` enters provider scope, invokes the method in place,
leaves scope, and discards the returned context/sentinel values.

Provider-owned context can be managed from Edict. `context_inspect!` returns a summary;
`context_reset!` clears conversation history while preserving the provider object and system
prompt. Inside `provider.repl()`, the same behavior is available as `/context` and `/reset`
slash commands.

```edict
provider < context_inspect! > / @summary
summary.messages to_json! print
provider < context_reset! > / /
```

Live Google/Gemma requests use `GEMINI_API_KEY` or `GOOGLE_API_KEY` when present:

```bash
EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.init([gemma-4-31b-it]) @provider provider < [Reply with exactly two lowercase letters: ok] request! > / / provider.assistant_text print'
```

### 2. Rollback-safe probing

`speculate [...]` runs code in an isolated checkpoint. Results can be inspected, but side
effects do not commit to the caller.

```bash
./build/edict/edict - <<'EDICT'
'baseline @answer
speculate ['candidate @answer answer] to_json! print
answer print
EDICT
```

Expected output:

```text
"candidate"
baseline
```

### 3. Logic query inside the VM

This example asks miniKanren for members of a list and serializes the result as JSON. Run it
from the project root after building:

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

Note: the interactive REPL compiles one line at a time. Use `edict -`, a script file, or `-e`
for multi-line object literals.

### 4. Import a native function at runtime

Cartographer can import a C header and shared library, then expose functions as Edict words:

```bash
./build/edict/edict - <<'EDICT'
'./build/cartographer/libagentmath_poc.so './cartographer/tests/libagentmath_poc.h resolver.import! @math
'3 '4 math.add! print
EDICT
```

Expected output:

```text
7
```

### 5. File/shell tools attached to provider context

The current standard-library slice exposes JSON-envelope file and shell helpers. Provider
objects carry `provider.tools`, so tool calls can be scoped to the same provider-owned state.

```edict
llm.init([local-qwen]) @provider
provider < [/tmp/agentc-note.txt] [hello] tools.write_file! @last_tool > / /
provider < [/tmp/agentc-note.txt] tools.read_file! @last_tool > / /
provider.last_tool.content print
```

---

## Current State

AgentC is usable today as an experimental local runtime with these working surfaces:

- Edict compiler, VM, REPL, contexts, rewrite rules, transactions, and `speculate [...]`.
- `edict --session ID` startup flag for named session create/resume under `/tmp/session/<id>/` by default.
- Persistent Listree/slab substrate with file-backed persistence work underway.
- Cartographer FFI imports for C/C++ headers and shared libraries.
- Embedded miniKanren runtime with Edict-callable query evaluation.
- Curated `edict.sh` launcher and Edict modules under `cpp-agent/edict/modules/`.
- Provider objects via `llm.init(...)` for local OpenAI-compatible models, Google/Gemini/Gemma,
  and OpenAI Codex.
- File/shell helper surface attached to provider objects.
- Streaming provider path using background native workers and main-thread synchronization.
- First thread-helper proof of concept: fresh worker VMs plus explicit shared cells.

Maturity notes:

- This is an alpha research system, not a stable packaged product.
- Examples are intended to run from a built checkout.
- Provider availability depends on credentials and local model/runtime configuration.
- Focused validation passes; some broad legacy test paths still contain active-investigation
  FFI callback/parser-map failures.

---

## Intern / Worker-Agent Direction

A major intended use case is local "intern" agents: smaller or local models that perform narrow
work for a primary agent without consuming the primary model's context budget.

Good intern tasks are bounded and checkable:

| Task type | Example |
|-----------|---------|
| Information gathering | Read several files and extract method signatures matching a pattern. |
| Failure triage | Run tests and classify failures by subsystem. |
| Search + filter | Find all callers of a function and discard irrelevant matches. |
| Compression | Summarize a large diff into semantic changes. |
| Context proxy | Hold a large context and answer factual questions about it. |

The target architecture is:

1. A coordinator VM owns the mutable root state.
2. Shared inputs/import tables are marked read-only before dispatch.
3. Each worker gets a fresh EdictVM and private workspace.
4. Re-entrant native tools can be shared; stateful provider handles remain coordinator-owned or
   are cloned per worker.
5. Workers return structured findings; the coordinator decides what to merge.

Existing pieces already point in this direction: read-only Listree flags, fresh worker VMs,
shared cells, stream queues, local provider presets, and provider-attached tools. The full
multi-intern scheduler is future work.

---

## Roadmap

### Near term

- Provider context management beyond the landed reset/inspect slice: trim and summarize conversation state.
- Better user-facing examples and notebook-style executable docs.
- More complete Edict ownership of agent root/session lifecycle.
- Hardened validation and cleanup of remaining legacy FFI test failures.
- First practical worker-agent scheduler built on fresh VMs, read-only sharing, and result
  queues.

### Longer term

- Durable mmap-backed resume for long-running agents.
- Curated native capability libraries for structural diffing, AST transforms, knowledge graphs,
  and repository analysis.
- Cartographer re-entrancy annotations so imported symbols can be safely shared across worker
  sandboxes.
- Multi-agent local execution where primary agents spend tokens deciding, not gathering.
- A tighter application-facing API around Edict-owned provider/session/tool loops.

---

## Repository Map

Most users will start with `edict.sh`, `cpp-agent/edict/modules/`, and the examples above.

```text
edict/          Edict compiler, VM, REPL, and tests
cpp-agent/      Provider runtime, persistence helpers, and Edict agent modules
cartographer/   Runtime C/C++ header import and FFI machinery
kanren/         Embedded miniKanren runtime
extensions/     File/shell/helper libraries importable from Edict
listree/        Persistent tree/list/value substrate
demo/           Demonstrations and shell smoke tests
LocalContext/   Project memory, design notes, goals, and current status
```

---

## More Documentation

- [`LocalContext/Dashboard.md`](LocalContext/Dashboard.md) — current project state and latest validation baseline
- [`LocalContext/Knowledge/WorkProducts/edict_language_reference.md`](LocalContext/Knowledge/WorkProducts/edict_language_reference.md) — detailed Edict language reference
- [`LocalContext/Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md`](LocalContext/Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) — LLM-oriented Edict/VM guide
- [`LocalContext/Knowledge/WorkProducts/WP-EdictResidentAgentLoopConsolidation-2026-05-10/index.md`](LocalContext/Knowledge/WorkProducts/WP-EdictResidentAgentLoopConsolidation-2026-05-10/index.md) — Edict-resident agent loop plan
