# Work Product: AgentC Architectural Vision & Intern Concurrency Model

## Purpose

Preserve the durable architectural vision for AgentC/J3 as a project WorkProduct: what makes the runtime distinctive, how the intern concurrency model works, what the current implementation boundary is, and what the resulting roadmap looks like. This document supersedes transient chat-context summaries and serves as the roadmap reference for intern concurrency planning.

## Status
**Authoritative** — created 2026-06-21 from the G098 goal closeout.

## Related Goals
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](../../Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — COMPLETE
- 🔗[G091 — Intern Worker Concurrency MVP](../../Goals/G091-InternWorkerConcurrencyMvp/index.md) — COMPLETE
- 🔗[G094 — Curated Native Cognitive Libraries](../../Goals/G094-CuratedNativeCognitiveLibraries/index.md) — COMPLETE
- 🔗[G095 — Edict Cognitive Skill Scaffolds](../../Goals/G095-EdictCognitiveSkillScaffolds/index.md) — COMPLETE
- 🔗[G097 — Composite Speculation + Logic + FFI Demo](../../Goals/G097-CompositeSpeculationLogicFfiDemo/index.md) — COMPLETE
- 🔗[G099 — Intern Task Quality Contracts](../../Goals/G099-InternTaskQualityContracts/index.md) — COMPLETE
- 🔗[G103 — Build-Time Static Core Declaration Image MVP](../../Goals/G103-BuildTimeStaticCoreDeclarationImageMvp/index.md) — COMPLETE
- 🔗[G104 — Immutable Code Object / Activation Frame Split](../../Goals/G104-ImmutableCodeObjectActivationFrameSplit/index.md) — COMPLETE
- 🔗[G105 — ReadOnly Static Slab Ownership Model](../../Goals/G105-ReadOnlyStaticSlabOwnershipModel/index.md) — COMPLETE
- 🔗[G106 — Root1 Slab Advertisement Registry](../../Goals/G106-Root1SlabAdvertisementRegistry/index.md) — COMPLETE
- 🔗[G107 — Process-Isolated Micro-VM Interns](../../Goals/G107-ProcessIsolatedMicroVmInterns/index.md) — COMPLETE
- 🔗[G108 — Cursor-Scoped Traversal Visit Bitmaps](../../Goals/G108-CursorScopedTraversalVisitBitmaps/index.md) — COMPLETE
- 🔗[G109 — Listree ReadOnly Mutation Surface Hardening](../../Goals/G109-ListreeReadOnlyMutationSurfaceHardening/index.md) — COMPLETE
- 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../../Goals/G110-EventfdEpollMicroVmIpcDesign/index.md) — COMPLETE
- 🔗[G111 — Root1/Worker Primitive FFI and Edict Intern Surface Migration](../../Goals/G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md) — COMPLETE

## Related Work Products
- 🔗[AgentLang](AgentLang.md) — original vision document
- 🔗[WP — Edict Native Agent Module Architecture](WP_EdictNativeAgentModuleArchitecture.md) — Edict-as-control-plane pivot
- 🔗[WP — Embedded Persistent Agent Architecture](WP_EmbeddedPersistentAgentArchitecture.md) — client/host/VM/slab split
- 🔗[Edict Language Reference](edict_language_reference.md) — canonical syntax reference
- 🔗[WP — LLM's Guide to Edict and the VM](WP-LlmsGuideToEdictVm-2026-05-10/index.md) — LLM-facing guide

---

## 1. Unique-Value Synthesis

AgentC/J3 is an internal-alpha persistent cognition runtime. Its distinctive claim is the composition of four primitives in one substrate:

1. **Persistence** — slab-allocated Listree arenas backed by mmap. Saving state is a memcpy; restoring is an mmap. Logical state survives process restart through file-backed slab images.
2. **Reversibility** — the same slab-watermark mechanism that provides persistence also provides O(1) rollback. `speculate [code]` and the C++ transaction API (`beginTransaction`/`rollbackTransaction`) let agents explore speculative branches and discard them cleanly.
3. **FFI** — the Cartographer service discovers native C/C++ library capabilities at runtime through header parsing and binary introspection, producing typed Edict-callable bindings without static glue code.
4. **Logic** — miniKanren is imported as an ordinary Cartographer-discovered library, not a compiler builtin. Logic queries are ordinary Listree objects evaluated through imported FFI.

No other agent runtime composes all four in a single memory model. JSON tool calls serialize and deserialize at every boundary; AgentC passes integer offsets into shared arenas. Ordinary LLM orchestration loops depend on transcript memory for state; AgentC persists structured Listree state across turns and process restarts.

---

## 2. Application Patterns

### 2.1 Edict-Resident Agent Loop

Edict owns agent policy. The canonical loop is:

```edict
llm.init([local-qwen]) @provider
provider < [Summarize the current task] request! > / /
provider.assistant_text print
```

The `provider < ... > / /` pattern scopes provider-mutating methods inside the provider context. The first `/` discards the provider context; the second discards the method sentinel.

### 2.2 Provider-Owned Conversation State

Provider objects carry conversation history, last response, streaming state, and context-management methods:

```edict
provider < context_inspect! > / @summary
summary.messages to_json! print
```

### 2.3 Cognitive Scaffolds (G095)

Reusable Edict-level state machines for agent workflows:

```edict
"parser-regression" cognitive.investigation_new! @investigation
investigation "H1" "cache-stale" cognitive.investigation_add_hypothesis! @investigation
investigation "H1" "edict/tests/vm_stack_tests.cpp:136" cognitive.investigation_add_evidence! @investigation
investigation to_json! @snapshot
```

Scaffold state is ordinary Listree — it serializes with `to_json!`, survives across VM executes, and can be snapshot/restored for branch isolation.

### 2.4 Composite FFI + Logic + State (G097)

```edict
[./libagentmath_poc.so] [./libagentmath_poc.h] resolver.import! @mathffi
[./libkanren.so] [./kanren_runtime_ffi_poc.h] resolver.import! @logicffi
logicffi.agentc_logic_eval_ltv @logic

10 32 mathffi.add! @native_sum /
{"fresh": ["q"], "where": [["membero", "q", ["42", "7"]], ["==", "q", "42"]], "results": ["q"], "limit": "1"} @logic_spec /
logic_spec logic! @answers
```

Native FFI produces a value; that value parameterizes a logic query; the result commits into durable Listree state. One substrate, one script, no serialization boundary.

### 2.5 Speculative Exploration

```edict
'baseline
speculate [trial]
-- stack: ["trial", "baseline"]
-- "baseline" is unchanged; "trial" came from the speculation
```

For scaffold-level branch isolation, use JSON snapshot/restore or the C++ transaction API — the same rollback substrate that backs `speculate` — to wrap full scaffold mutation with guaranteed parent-state restoration.

---

## 3. Intern Concurrency Model

### 3.1 Coordinator/Worker Boundary

The coordinator VM owns mutable root state. Intern workers run bounded Edict programs in fresh worker VMs with private `workspace`, read-only shared `context`/`imports`, and JSON-snapshotted `input`. Results are serialized to JSON in the worker and parsed into fresh coordinator-owned Listree values after join.

**Synchronous dispatch:**

```edict
worker_task intern_run! @worker_result
```

**Asynchronous dispatch:**

```edict
worker_task intern_start! @job
job.job_id intern_sync! @status
job.job_id intern_cancel! @cancel_status
```

### 3.2 Task Envelope

| Field | Required | Meaning |
|-------|----------|---------|
| `task_id` / `id` | optional | Stable label copied into the result. |
| `program` | required | Bounded Edict source executed inside the worker VM. |
| `input` | optional | JSON-snapshotted private worker input. |
| `context` | optional | Shared context subtree; recursively frozen before dispatch. |
| `imports` | optional | Shared import namespace; recursively frozen before dispatch. |
| `max_active_jobs` | optional | Async backpressure limit. |
| `expect.success_field` | required for `intern_start!` | First machine-checkable success criterion. |
| `expect.min_evidence_count` | optional | Result-trust threshold. |
| `expect.min_confidence` | optional | Result-trust threshold over `low < medium < high`. |
| `limits.max_result_bytes` | required for `intern_start!` | Bound against over-broad results. |

### 3.3 Worker Root

| Field | Meaning |
|-------|---------|
| `task_id` | Copied task id. |
| `input` | Private JSON snapshot. |
| `context` | Read-only shared subtree. |
| `imports` | Read-only shared import namespace. |
| `workspace` | Private mutable scratch object. |
| `result` | Program-assigned structured result; if absent, stack top is used. |

### 3.4 Result Envelope

| Field | Meaning |
|-------|---------|
| `ok` | `["ok"]` on success, `[]` on failure. |
| `task_id` | Task id. |
| `state` | `complete`, `error`, `started`, `running`, `cancel_requested`, `cancelled`, or `backpressure`. |
| `worker` | Backend name (`edict-thread`, `edict-thread-async`). |
| `result` | Worker result copied back through JSON on the coordinator thread. |
| `error` | Error object or null. |
| `safety` | Read-only/snapshot/merge-thread metadata. |
| `publication` | Reserved for future slab publication handles; currently null. |

### 3.5 Lifecycle and Safety Rules

- Shared `context`/`imports` are recursively `ReadOnly` before worker launch.
- Worker input is a JSON snapshot — worker mutation does not affect coordinator-owned input.
- Worker output is serialized to JSON in the worker and parsed into a fresh coordinator-owned `ListreeValue` after join.
- Do not pass stateful provider handles, raw fds, credentials, VM pointers, or activation frames through context/imports.
- `intern_cancel!` is cooperative and non-retroactive: if the request is accepted before terminal completion, final sync returns `cancelled`; if the worker is already terminal, the complete/error result remains visible.
- Terminal async jobs are retained for repeated sync until explicit `worker.edict_drop!` or bounded retention sweep.
- Dropping a running job abandons the handle and suppresses orphan completion publication.
- `worker.edict_lifecycle_status!` reports active/tracked/retained-terminal/abandoned-running counts and cumulative started/finished/abandoned/dropped/swept counters.

### 3.6 Process Isolation (G107)

Intern workers can run in three isolation tiers:

1. **Thread worker** (`edict-thread`) — same process, fresh VM, shared slab arena. Default MVP backend.
2. **Forked worker** (`edict-fork-async`) — child process via `fork()`, inherited frozen/static-immortal shared base, read-only `static_mounts` descriptors. Outcome collected through anonymous pipe.
3. **Exec worker** (`edict-exec-async`) — independent `fork()`/`exec()` of `edict_worker_exec`, independently mmap-mounts a G103 static declaration image, resolves static program/bytecode entries from the mounted image. True OS-backed isolation.

Handle/capability policy for process-isolated workers:

| Category | Examples | Policy |
|----------|----------|--------|
| **Inherited** | Frozen read-only Listree, static-immortal slabs | Safe via fork COW |
| **Rehydrated** | context/imports/input via JSON pipe, static_mounts via independent mmap | Safe by construction |
| **Blocked** | Raw fds, provider handles, credentials, VM pointers, activation frames, dlopen handles | Never cross coordinator→worker boundary |

### 3.7 Root1 Broker Integration (G110)

The async intern path is broker-compatible with the Root1 eventfd/epoll resource broker:

- `intern_start!` creates a Root1 participant, registers a waitable, and launches the worker.
- The worker publishes `Complete`/`Error` descriptors through the participant mailbox.
- `intern_sync!` drains mailbox descriptors on the coordinator thread.
- `intern_cancel!` sends a `Cancelled` descriptor through the broker.
- `await!` parks through `Root1AwaitScheduler`; scheduler state is persisted with named sessions.

---

## 4. Layered mmap Micro-VM Architecture

The longer-term memory substrate envisions layered mmap-backed arenas:

```text
private dynamic VM layer
    writable by one active VM
    contains stacks, task root, workspace, transient execution frames

mutable coordination/mailbox layer (Root1-brokered)
    explicit shared coordination cells, rings, descriptors
    waitable through Root1 eventfd/epoll broker

published communication layer (optional)
    owner-writable before publication
    read-only to other VMs after publication
    contains immutable result subtrees

static core slab layer
    built once by a build-time Root0 image builder
    mmap'ed read-only by all worker/coordinator VMs
    contains import dictionaries, module definitions, compiled thunks
```

Lookup walks from private to group to core. Writes go only to the private layer or to an explicitly owned/broker-granted mutable slab. Static/core and already-published result layers are immutable to readers.

G103 (static declaration image MVP), G105 (ReadOnly static slab ownership), and G106 (Root1 publication registry) are the first concrete slices of this vision. G107 proves process-isolated workers can independently mmap static declaration images and execute static program/bytecode entries from them.

---

## 5. Persistence and Session Resume (G096)

Durable state lives in the embedded VM's mmap-backed Listree object graph:

- **Durable**: conversation state, agent loop state, policy configuration, model/provider defaults, memory/dictionary structures, scheduler continuation tables, static mount metadata.
- **Transient** (rebuilt on restore): runtime library handles, provider HTTP clients, sockets, SSE parsers, FFI dynamic-library handles, native pointers.

`./build/edict/edict --session ID` creates/resumes a root scope under `/tmp/session/<id>/`. Session state survives normal process exit through the authoritative session-image/slab store. G096 covers deterministic root/scheduler/static-mount resume. Full kill-mid-op activation-frame resurrection remains future work.

---

## 6. Cognitive Capability Libraries (G094)

G094 delivered curated native cognitive capabilities through tree-sitter and knowledge graph integration:

- **Tree-sitter bridge**: `treesitter.load!` / `treesitter.parse!` / `treesitter.list!` / `treesitter.diff!` — AST parsing and structural diffing for C, Python, and Edict source.
- **Knowledge graph**: `kgraph.create!` / `kgraph.add_node!` / `kgraph.add_edge!` / `kgraph.get_node!` / `kgraph.query!` / `kgraph.nodes!` / `kgraph.edges!` — persistent Listree-backed graph for code structure relationships.

These are ordinary Edict builtins backed by imported native libraries, not compiler intrinsics. They compose with speculation, transactions, and persistent state like any other Edict words.

---

## 7. Current Implementation Boundary

### What is real and useful now

- **Rollbackable scratch execution**: `speculate [code]` and C++ transactions provide isolated, reversible execution. Directly useful for speculative planning.
- **Unified internal state**: Listree + Cursor provide a traversable memory model for tasks, hypotheses, file relationships, and execution context.
- **Dynamic native interop**: Cartographer + libffi provide a working path for reflecting native C/C++ capabilities into the runtime.
- **Logic and rewrite primitives**: miniKanren and runtime rewrite rules exist as bounded reasoning and compression tools.
- **Allocator-layer persistence**: mmap-backed slab persistence with structured restore for non-trivial Listree state.
- **Process-isolated workers**: fork and fork/exec worker backends with static declaration image mounting.
- **Cognitive scaffolds**: investigation, code-review, and refactor-plan state machines with JSON serialization.
- **Quality contracts**: task-envelope validation, result trust validators, evidence/confidence thresholds.

### What remains future work

- **Shared-arena sidecar handoff**: whole-VM/root restore across processes is still unfinished.
- **Cartographer as general capability service**: no mature sidecar protocol, safety model, or full binary-introspection path yet.
- **Mini-Kanren as full planner**: still limited by eager search and narrow surface syntax.
- **Term rewriting as self-optimizing compiler**: currently runtime token-sequence transformation, not deep semantic optimization.
- **Full activation-frame resurrection**: kill-mid-op serialization and stable cross-version code-object identity.
- **Published result slabs**: zero-copy immutable result sharing through Root1-advertised slab publications.
- **Durable async job records**: intern job state surviving process restart.

---

## 8. Recommended Positioning

AgentC is most compelling as the agent's **internal cognitive operating environment**, not as a wholesale replacement for the outer interactive coding runtime.

The outer agent loop should continue to handle:
- user interaction,
- tool selection,
- shell and build orchestration,
- file edits,
- git workflow.

AgentC should handle what current coding agents do poorly:
- reversible speculative state,
- compact intermediate memory,
- relational constraint solving,
- persistent cognitive scratchpads,
- typed capability mediation for selected tools,
- bounded intern workers for extraction/summarization/classification.

**The user-facing loop stays tool-driven, while the internal reasoning loop becomes arena-backed, structured, and reversible.**

---

## 9. Roadmap Summary

### Completed tracks
- **G078**: Edict-resident agent loop consolidation — Edict owns provider/session/tool/context semantics.
- **G091**: Intern worker concurrency MVP — blocking/async worker dispatch, lifecycle/drop/abandon, cancellation.
- **G094**: Curated native cognitive libraries — tree-sitter, structural diff, knowledge graph.
- **G095**: Cognitive skill scaffolds — investigation/review/refactor state machines.
- **G096**: Authoritative mmap session resume — deterministic root/scheduler/static-mount restore.
- **G097**: Composite speculation + logic + FFI demo — one-command composition proof.
- **G099**: Intern task quality contracts — bounded schemas, dispatch rejection, result validators.
- **G103–G106**: Static declaration images, ReadOnly slab ownership, immutable code objects, Root1 publication registry.
- **G107**: Process-isolated micro-VM interns — thread/fork/exec workers with static image mounting.
- **G108–G111**: Traversal bitmaps, ReadOnly hardening, Root1 broker, worker primitive FFI migration.

### Remaining goals
- **G098** (this WorkProduct): Architectural vision preservation — COMPLETE.
- **G075** (deferred): Speculative Edict native architectures — ToT/MCTS/ReAct-style speculation after loop/tool/context stability.
- **G093** (deferred): Reference-scoped ReadOnly sharing — finer-grained shadowing after whole-subtree sharing proves insufficient.

---

## Edict Syntax Conventions

All snippets in this document use current Edict syntax:

- **Word invocation**: `word!` or `module.word!` (adjacent `!` is the documented idiom).
- **Stack discard**: `/` (bare slash, not legacy English spelling).
- **Provider scope**: `provider < [prompt] request! > / /` (double `/ /` to discard context and sentinel).
- **Assignment**: `value @name` (pushes value, then assigns to named slot).
- **Dictionary append**: `value ^list^` (prepends value to list-mode field).
- **JSON literals**: `{"key": "value"}` parsed by the compiler.
- **Speculation**: `speculate [code]` runs code in an isolated snapshot.
- **Thunks**: `[code] @name` captures a code block; `name!` executes it.
- **Import**: `[lib_path] [header_path] resolver.import! @namespace` or `resolver.import_resolved! @namespace`.
- **Logic**: `logic_spec logic!` or `logicffi.agentc_logic_eval_ltv!` evaluates a miniKanren spec.
