# AgentC: Persistent Cognition for AI Agents

AgentC is an experimental runtime for agents that need more than prompt text, JSON tool
calls, and short-lived scratchpads. It gives an agent a persistent memory graph, a compact
native language ("Edict", for Executable Dictionary), rollback-safe execution, dynamic
access to C/C++ capabilities, sandboxed compilation of agent-generated C (TinyCC), embedded
logic programming, cognitive skill scaffolds, and local worker agents in one substrate.

Edict, AgentC's VM language, is the
agent control plane. Provider sessions, tool use, context management, speculative branches,
delegated worker agents, and cognitive state machines all live inside the VM. C++ remains the
native layer for memory, persistence, credentials, transports, and unsafe system capabilities.

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
- **Maintain structured cognitive state** (hypotheses, evidence, findings) across turns using
  durable scaffold objects that serialize to JSON.

AgentC is not just another chat wrapper. It is a candidate operating environment for agents
whose memory, actions, constraints, and provider state should live together.

---

## What Makes AgentC Different

### Persistent cognitive memory

AgentC stores state in arena-backed Listree nodes. Pointers are relative offsets, so state can
be persisted through file-backed slabs and restored without reconstructing an object graph.
Named sessions (`edict --session ID`) survive process restart through deterministic
root/scheduler/static-mount resume.

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

### Sandboxed generated C (TinyCC)

Edict programs can compile agent-authored C source at runtime through the `tcc` capsule.
Modules resolve external symbols only through an explicit allowlist
(`tcc.allow_process_symbol!` / `tcc.allow_library_symbol!`), enter through a fixed C ABI
(`agentc_tcc_entry`), and can run in-process or in isolated worker processes with timeouts,
cooperative cancellation, and crash containment. The TinyCC path is deliberately independent
of Cartographer/libffi.

### Logic programming inside the agent

miniKanren is embedded alongside Edict. Logic queries share the same memory substrate and use
the same rollback mechanics. Constraint solving can be part of the agent's thought process, not
an external microservice.

### Cognitive skill scaffolds

Reusable Edict-level state machines for investigation, code review, and refactor planning.
Scaffold state is ordinary Listree — it serializes with `to_json!`, survives across VM
executes, and can be snapshot/restored for branch isolation.

### Reference-scoped ReadOnly sharing

Overlay dictionaries let worker agents shadow specific keys on a frozen shared configuration
without mutating the coordinator's state. The frozen base stays ReadOnly; the worker's shadow
values are mutable and inspectable through `overlay.commit!`.

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
| Multi-branch reasoning | Speculative execution with slab rollback enables ToT/MCTS-style exploration (future). |

---

## Mental Model

AgentC has four user-visible layers:

```text
Agent / user intent
        │
        ▼
Edict control plane
  provider loops, tools, memory updates, speculation, logic calls,
  cognitive scaffolds, overlay dictionaries, intern worker dispatch
        │
        ▼
Persistent Listree state
  one graph for objects, scopes, lists, runtime data, imported capabilities
        │
        ▼
Native substrate
  mmap/slabs, C/C++ FFI, provider transports, credentials, file/shell helpers,
  Root1 eventfd/epoll broker, process-isolated workers
```

The important shift is that the agent is not just asking a host process to run tools. The agent
can increasingly *own the loop* inside Edict: maintain provider state, mutate structured memory,
call native code, probe branches, delegate to workers, and decide what to keep.

---

## Quick Start

Prerequisites:

- C++17 compiler
- CMake 3.16+
- `libffi-dev`
- `libclang-dev`
- `libcurl` development package
- POSIX-like environment with pthreads
- `libtcc` (optional; enables the generated-C `tcc` capsule — auto-detected, controlled by
  `AGENTC_ENABLE_TCC` / `AGENTC_TCC_ROOT`)

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

### 6. Cognitive skill scaffolds

Investigation, code-review, and refactor-plan state machines with JSON-serializable state:

```edict
"parser-regression" cognitive.investigation_new! @investigation
investigation "H1" "cache-stale" cognitive.investigation_add_hypothesis! @investigation
investigation "H1" "edict/tests/vm_stack_tests.cpp:136" cognitive.investigation_add_evidence! @investigation
investigation to_json! @snapshot
snapshot print
```

### 7. Overlay dictionaries for worker-local shadow values

Workers can shadow specific keys on a frozen shared configuration without mutating the
coordinator's state:

```edict
{"model": "gpt-4", "temperature": "0.7"} @config
config freeze! @frozen_config
frozen_config overlay.new! @worker_view
worker_view "temperature" "0.2" overlay.set! @worker_view
worker_view "temperature" overlay.get! print
worker_view "model" overlay.get! print
frozen_config.temperature print
```

Expected output:

```text
0.2
gpt-4
0.7
```

### 8. Sandboxed generated C (TinyCC)

When built with libtcc, the agent can compile and run its own C helpers at runtime:

```bash
./build/edict/edict - <<'EDICT'
[int agentc_tcc_entry(agentc_tcc_call* call){
    long long v = agentc_tcc_parse_i64(agentc_tcc_arg_text(call, 0));
    agentc_tcc_result_i64(call, v * 2);
    return 0;
}] tcc.compile! @mod
mod [["21"]] from_json! tcc.run! @res
res.result_i64 print
EDICT
```

Expected output:

```text
42
```

The built-in `agentc_tcc_*` ABI helpers are always available to modules. Any other external
symbol must be allowlisted first with `tcc.allow_process_symbol!` or
`tcc.allow_library_symbol!`; unauthorized references fail at relocation. For untrusted or
long-running modules, `tcc.start_isolated!` runs the entry point in a separate worker process
with a timeout, and `tcc.status!`/`tcc.collect!`/`tcc.cancel!` manage the job.

### 9. Composite FFI + logic + state

```bash
./build/demo/demo_composite_speculation_logic_ffi
```

This demo composes Cartographer-imported native FFI (`add(10,32)=42`), a miniKanren query
parameterized by the native result, and durable Listree state commitment in one deterministic
script.

---

## Current State

AgentC is usable today as an experimental local runtime with these working surfaces:

- Edict compiler, VM, REPL, contexts, rewrite rules, transactions, and `speculate [...]`.
- `edict --session ID` for named session create/resume with deterministic root/scheduler/static-mount restore.
- Persistent Listree/slab substrate with file-backed persistence and mmap-backed session resume.
- Cartographer FFI imports for C/C++ headers and shared libraries, with re-entrancy capability metadata.
- Embedded miniKanren runtime with Edict-callable query evaluation.
- Tree-sitter bridge: `treesitter.load!`/`parse!`/`list!`/`diff!` for AST parsing and structural diffing.
- Persistent knowledge graph: `kgraph.create!`/`add_node!`/`add_edge!`/`get_node!`/`query!`/`nodes!`/`edges!`.
- Cognitive skill scaffolds: investigation, code-review, and refactor-plan state machines with JSON serialization.
- Overlay dictionaries: `overlay.new!`/`set!`/`get!`/`has!`/`keys!`/`shadow_keys!`/`commit!` for reference-scoped ReadOnly sharing.
- TinyCC generated-C interop: `tcc.compile!`/`run!`/`symbols!`/`drop!` plus isolated jobs (`start_isolated!`/`status!`/`collect!`/`cancel!`) gated by per-symbol allowlists (`allow_process_symbol!`/`allow_library_symbol!`/`clear_symbols!`).
- Curated `edict.sh` launcher and Edict modules under `cpp-agent/edict/modules/`.
- Provider objects via `llm.init(...)` for local OpenAI-compatible models, Google/Gemini/Gemma, and OpenAI Codex.
- File/shell helper surface attached to provider objects.
- Streaming provider path using background native workers and main-thread synchronization.
- Intern worker substrate: `intern_run!` (blocking), `intern_start!`/`intern_sync!`/`intern_cancel!` (async), with quality contracts and backpressure.
- Process-isolated workers: thread, fork, and fork/exec backends with independently mounted static declaration images.
- Root1 eventfd/epoll resource broker with mailbox descriptors, `await!`, and scheduler persistence.
- Composite demos: `demo_composite_speculation_logic_ffi`, `demo_overlay_dictionary`, `demo_cognitive_core_validation`.

Maturity notes:

- This is an alpha research system, not a stable packaged product.
- Examples are intended to run from a built checkout.
- Provider availability depends on credentials and local model/runtime configuration.
- Full validation baseline (verified 2026-07-07): `edict_tests` 210/210, `reflect_tests` 55/55, `listree_tests` 83/83, `cartographer_tests` 52/52, `kanren_tests` 14/14, `treesitter_tests` 28/28, `markethub_tests` 12/12. `cpp_agent_tests` (56 tests) additionally requires a live local provider endpoint for its provider-loop suites.

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

The landed substrate includes module-backed deterministic `intern_run!` plus async job dispatch.
Load `worker.edict` / `intern.edict` over the worker primitive import before using these words:

```edict
# task envelope shape: task_id, program, input, context, optional imports
worker_task intern_run! @worker_result

worker_task intern_start! @job
job.job_id intern_sync! @status
job.job_id intern_cancel! @cancel_status
```

`intern_run!` freezes `context` and `imports`, snapshots `input`, launches a fresh worker
`EdictVM` with a private `workspace`, executes the bounded Edict `program`, joins the worker,
and returns a structured envelope. Workers can also run as forked or fork/exec processes with
independently mounted static declaration images (G107). Quality contracts enforce bounded task
schemas and result trust validators (G099). Overlay dictionaries provide reference-scoped
ReadOnly sharing so workers can shadow specific keys without mutating coordinator state (G093).

The target architecture:

1. A coordinator VM owns the mutable root state.
2. Shared context/import tables are marked read-only before dispatch; workers can use overlay
   dictionaries for per-key shadow values.
3. Each worker gets a fresh EdictVM and private workspace (thread, fork, or fork/exec).
4. Re-entrant native tools can be shared; stateful provider handles remain coordinator-owned.
5. Workers return structured findings; the coordinator decides what to merge.

---

## Roadmap

### Completed

- Edict-resident agent loop consolidation (G078): provider/session/tool/context semantics.
- Intern worker concurrency MVP (G091): blocking/async dispatch, lifecycle/drop/abandon, cancellation.
- Reference-scoped ReadOnly sharing (G093): overlay dictionaries with 7 VM opcodes.
- Curated native cognitive libraries (G094): tree-sitter, structural diff, knowledge graph.
- Cognitive skill scaffolds (G095): investigation/code-review/refactor-plan state machines.
- Authoritative mmap session resume (G096): deterministic root/scheduler/static-mount restore.
- Composite speculation + logic + FFI demo (G097).
- Architectural vision work product (G098).
- Intern task quality contracts (G099): bounded schemas, dispatch rejection, result validators.
- Edict isolation contract hardening (G100).
- Direct Edict tool-emission path (G101).
- Session ID startup flag (G102).
- Static declaration image MVP (G103), immutable code objects (G104), ReadOnly slab ownership (G105).
- Root1 slab advertisement registry (G106).
- Process-isolated micro-VM interns (G107): thread/fork/exec workers.
- Cursor-scoped traversal visit bitmaps (G108).
- Listree ReadOnly mutation surface hardening (G109).
- Root1 eventfd/epoll resource broker and micro-VM IPC design (G110).
- Root1/worker primitive FFI and Edict intern surface migration (G111).
- TinyCC native interop track (G112–G116): build probe, fixed-ABI runtime service, Edict `tcc` surface, isolated micro-VM execution, symbol-cache/C-ABI bridge with allowlists.
- Market-data hub consumer module `markethub/` with DeltaGUI façade bridge (G117, Phases 0–3).
- TCC IPC and symbol-resolution deduplication (G118).

### Remaining

- **G075 — Speculative Edict Native Architectures** (deferred): ToT/MCTS/ReAct-style multi-branch
  speculation using slab snapshot/rollback. The prerequisite control-plane stability and worker
  isolation are now in place. Activate when a concrete multi-branch reasoning demonstration is
  needed.

### Future work (not yet formalized as goals)

- Published result slabs: zero-copy immutable result sharing through Root1-advertised slab publications.
- Durable async job records: intern job state surviving process restart.
- Full activation-frame resurrection: kill-mid-op serialization and stable cross-version code-object identity.
- Shared-arena sidecar handoff: whole-VM/root restore across processes.
- Cartographer as general capability service: mature sidecar protocol and full binary introspection.
- Better user-facing examples and notebook-style executable docs.
- A tighter application-facing API around Edict-owned provider/session/tool loops.

---

## Repository Map

Most users will start with `edict.sh`, `cpp-agent/edict/modules/`, and the examples above.

```text
edict/          Edict compiler, VM, REPL, TinyCC runtime, and tests
cpp-agent/      Provider runtime, persistence helpers, and Edict agent modules
cartographer/   Runtime C/C++ header import and FFI machinery
kanren/         Embedded miniKanren runtime
extensions/     File/shell/helper libraries importable from Edict
core/           Slab allocator, cursor, and shared substrate primitives
listree/        Persistent tree/list/value substrate
treesitter/     Tree-sitter grammar and bridge for AST parsing/diffing
markethub/      Market-data hub consumer module (one-way dependency on AgentC)
demo/           Demonstrations and shell smoke tests
LocalContext/   Project memory, design notes, goals, and current status
```

---

## More Documentation

- [`LocalContext/Dashboard.md`](LocalContext/Dashboard.md) — current project state and latest validation baseline
- [`LocalContext/Knowledge/WorkProducts/edict_language_reference.md`](LocalContext/Knowledge/WorkProducts/edict_language_reference.md) — detailed Edict language reference
- [`LocalContext/Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md`](LocalContext/Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) — LLM-oriented Edict/VM guide
- [`LocalContext/Knowledge/WorkProducts/WP-AgentCArchitecturalVisionInternConcurrency-2026-06-21.md`](LocalContext/Knowledge/WorkProducts/WP-AgentCArchitecturalVisionInternConcurrency-2026-06-21.md) — architectural vision and intern concurrency model
