# Work Product: Edict-Native Agent Module Architecture

## Purpose
Define the next-stage AgentC runtime architecture in which **Edict becomes the fundamental agent runtime and UI substrate**, while LLM transport/provider logic moves behind a reusable native runtime library exposed to Edict as an importable `agentc` module.

## Status
**Active design / authoritative plan for G068**

## Supersedes / Refines
This plan refines and partially supersedes the earlier assumption that the long-lived host process should own a hardcoded outer C++ agent loop. That outer loop was a useful proving step for provider integration, socket daemonization, and embedded VM coupling, but it is no longer the clean target architecture.

What remains valid from the earlier architecture:
- keep the **Client ↔ Host** process boundary,
- keep the **Edict VM embedded in the host process**,
- use **mmap slab persistence** for durable logical state,
- rehydrate transient runtime artifacts on restore.

What changes in this plan:
- the canonical **agent loop moves into Edict**,
- the host process becomes an **interpreter/runtime host** rather than the owner of agent policy,
- LLM/provider code becomes a **shared runtime capability** rather than a UI-specific executable concern,
- `agentc` becomes an **importable Edict module/capability namespace**.

## Core Architectural Pivot

### Previous working model
```text
[Client / Socket UI]
      ⇅
[cpp-agent host]
  - outer C++ agent loop
  - provider dispatch
  - tool dispatch
  - embedded Edict VM
```

### New target model
```text
[Shell | Socket | Pipe | File | Batch Front-End]
                  ⇅ stream / request input
        [Edict Host Process / Interpreter Front-End]
                  |
                  | in-process API
                  v
             [Embedded Edict VM]
               - durable cognitive state
               - canonical agent loop
               - tool / memory / logic policy
               - transaction / rewrite / import facilities
               - importable `agentc` module
                  |
                  | native runtime bridge
                  v
            [libagent_runtime.so]
               - provider selection
               - credentials
               - HTTP transport
               - response normalization
               - JSON return buffer
                  |
                  v
          [Gemini / OpenAI / Copilot / future providers]
```

## Architectural Rules
1. **Edict owns agent policy.**
   - Prompting/orchestration logic lives in Edict, not in a hardcoded outer C++ loop.
2. **The runtime library owns transport, not behavior.**
   - Native code performs provider I/O, auth, streaming assembly, normalization, and buffer management.
3. **`agentc` is an Edict capability/module.**
   - It is the LLM-facing interface visible to Edict programs.
4. **Responses are data first.**
   - Model output is normalized JSON, converted into Edict/Listree values, and only then interpreted under Edict policy.
5. **Unsafe capability escalation is opt-in.**
   - Arbitrary model-driven FFI import/use remains disabled by default.
6. **Persistence remains slab-first.**
   - Durable logical state belongs in the embedded VM/slab; runtime handles and transport state are transient.

## Why This Is Cleaner

### 1. Clear separation of concerns
- **Front-ends** handle attachment and rendering.
- **Edict** handles planning, memory, logic, policy, and control flow.
- **`libagent_runtime.so`** handles provider/auth/transport mechanics.

### 2. Better modularity
The same Edict-native agent can be surfaced through:
- direct shell / REPL,
- Unix socket daemon,
- stdin/stdout pipe,
- batch file execution,
- future TUI integration,
without rewriting the agent logic for each transport.

### 3. Better persistence semantics
The meaningful state of an agent turn becomes ordinary Edict state:
- conversation memory,
- planning state,
- retry counters,
- working dictionaries,
- chosen policies,
- model/provider preferences,
- resumable task state.

### 4. Better language-level leverage
If Edict directly receives normalized LLM output as structured data, it can immediately use:
- dictionary storage/retrieval,
- transactions/speculation,
- rewrite rules,
- resolver/import facilities,
- logic queries,
- curated FFI wrappers,
without maintaining a duplicated outer orchestration system in C++.

## Importable Module Model
The canonical LLM interface is an Edict namespace/module named `agentc`.

### Initial surface
- `agentc.call(request)`
  - Send a prompt/request object to the selected provider.
  - Return a normalized Edict value parsed from JSON.
- `agentc.configure(config)`
  - Set default provider/model/policy values in VM state.
- `agentc.last()`
  - Return the last normalized request/response metadata for debugging or tracing.

### Later optional surface
- `agentc.exec(request)`
  - Convenience wrapper that may execute a validated response directive under policy.
- `agentc.stream(request, handler)`
  - Streaming mode once the structured event contract is stabilized.
- `agentc.cancel(request_id)`
  - Cancellation for long-running requests.

### Critical design decision
The first production slice should use **`agentc.call(request) -> value`** as the primitive. That keeps the LLM boundary data-oriented and makes Edict explicitly responsible for all execution policy.

## Runtime Library Boundary
`libagent_runtime.so` should expose a **small C ABI** so it can be loaded through a stable boundary and reused by multiple hosts.

### Minimal conceptual ABI
```c
typedef void* agentc_runtime_t;

agentc_runtime_t agentc_runtime_create(const char* config_json);
void agentc_runtime_destroy(agentc_runtime_t rt);

char* agentc_runtime_request(agentc_runtime_t rt, const char* request_json);
void agentc_runtime_free_string(char* s);
```

### Responsibilities of the runtime library
- provider selection,
- credentials/auth lookup,
- HTTP request execution,
- optional SSE accumulation,
- normalization of vendor responses to one contract,
- JSON serialization of the normalized result,
- no agent policy beyond bounded request safety.

### Responsibilities that must remain outside the runtime library
- conversation policy,
- tool sequencing,
- memory routing,
- multi-step planning,
- state mutation policy,
- capability authorization decisions.

## Response Contract
Edict can directly read JSON, but vendor-specific payloads should not become the long-term execution contract. The runtime library should normalize all provider responses into a single schema before Edict consumes them.

### Recommended normalized request shape
```json
{
  "prompt": "Summarize the current task",
  "provider": "google",
  "model": "gemini-2.5-pro",
  "system": "optional system prompt",
  "conversation": [],
  "response_mode": "json",
  "limits": {
    "max_output_tokens": 2048,
    "timeout_ms": 30000
  }
}
```

### Recommended normalized response shape
```json
{
  "ok": true,
  "request_id": "req-123",
  "provider": "google",
  "model": "gemini-2.5-pro",
  "finish_reason": "stop",
  "message": {
    "role": "assistant",
    "text": "Hello world"
  },
  "tool_calls": [],
  "usage": {
    "input_tokens": 0,
    "output_tokens": 0
  },
  "raw": null
}
```

### Optional directive envelope for Edict-driven control
Once the base data path is stable, Edict can ask models to emit structured directives such as:
```json
{
  "kind": "agent_step",
  "directive": {
    "type": "reply",
    "text": "Hello world"
  }
}
```

Other directive types may include:
- `reply`
- `continue`
- `tool_call`
- `memory_lookup`
- `memory_write`
- `emit_program`

But Edict must always validate these before acting.

## Execution Modes

### Mode 1 — Data-only mode (initial default)
- `agentc.call(...)` returns structured data.
- Edict examines it and decides what to do.
- No model output is treated as executable by default.

### Mode 2 — Controlled program-emission mode
- The model may return a `program` or structured directive field.
- Edict validates the payload and executes it only if policy permits.

### Mode 3 — Unrestricted model-driven interpreter control
- The model may use broad interpreter and FFI facilities directly.
- **This mode exists conceptually but remains disabled initially.**
- Enabling it should be an explicit policy switch, not an accidental default.

## Capability Safety Model

### Tier 1 — Safe default
Available to model-directed Edict logic by default:
- `agentc.call`
- dictionary/memory read-write within VM policy
- logic queries
- transactions/speculation
- curated safe stdlib
- curated safe imported modules

### Tier 2 — Privileged curated native capabilities
Allowed only through explicit policy/configuration:
- selected resolver/import facilities,
- selected native modules,
- selected filesystem/network helpers.

### Tier 3 — Unsafe / unrestricted native access
Disabled by default:
- arbitrary FFI import,
- arbitrary symbol resolution,
- unrestricted native side effects,
- unrestricted recursive capability escalation.

### Required guards
- request/turn budget,
- recursion depth limit,
- token budget,
- timeout budget,
- cancellation path,
- capability allowlist,
- execution-mode switch.

## Persistence and Rehydration Model

The Edict-native agent loop continues to rely on the previously chosen **mmap-backed Listree/slab persistence** model. This plan changes where orchestration lives, not the durable substrate: persistent logical state still lives in the embedded VM's mmap-backed Listree object graph and is restored through the slab/root-state path.

### Durable state in slab
- conversation state,
- agent loop state,
- policy configuration,
- model/provider defaults,
- memory/dictionary/Listree structures,
- staged directives,
- resumable workflow/task state,
- enough metadata to re-import curated modules safely.

### Transient state rebuilt on restore
- runtime library handles,
- provider HTTP clients,
- sockets,
- SSE parsers,
- in-flight request state,
- FFI dynamic-library handles,
- native pointers/function pointers.

### Important implication
The embedded VM persists the logical idea of what is loaded/configured, but host startup is responsible for rehydrating the live native runtime objects that make those logical capabilities executable.

## Host / UI Model
Edict returns as the fundamental user-facing interface. The same runtime can be presented through multiple shells around the same embedded interpreter model.

### Supported front-end shapes
- **Direct shell interface** — interactive interpreter / REPL
- **Socket interface** — reconnectable daemon front-end over UDS
- **Pipe interface** — stdin/stdout streaming integration
- **File / batch interface** — run scripted workflows against persisted state
- **Future TUI** — richer presentation without changing the core agent policy

### Consequence
`cpp-agent` no longer needs to be the place where agent behavior lives. It can instead become one of several thin host/front-end shells over the same Edict-native agent substrate.

## Relationship to Current Codebase

### Existing pieces that remain valuable
- `cpp-agent/providers/*.cpp` — provider knowledge
- `cpp-agent/http_client.*` — transport substrate
- `cpp-agent/credentials.*` — auth/credential loading
- `cpp-agent/socket_server.*` — reconnectable socket front-end
- embedded `libedict` integration — correct long-term process boundary
- slab persistence groundwork from G041/G042/G069

### Pieces that become transitional rather than canonical
- `cpp-agent/agent_loop.cpp`
- outer C++ tool-dispatch ownership
- UI-specific orchestration in `cpp-agent/main.cpp`

### Refactoring direction
1. Extract reusable provider/runtime code into `libagent_runtime.so`.
2. Expose a stable Edict-facing `agentc` capability/module.
3. Move canonical agent policy into Edict source/modules.
4. Reduce `cpp-agent` to a host/front-end and migration harness.

## Staged Implementation Plan

### Phase 1 — Runtime extraction
**Goal:** carve provider/auth/HTTP code out of the UI host.
- Extract transport/provider code into `libagent_runtime.so`.
- Define and test a small C ABI.
- Normalize provider responses into one JSON contract.
- Preserve current Gemini/OpenAI/Copilot support.

### Phase 2 — Edict module surface
**Goal:** make LLM access a native Edict capability.
- Add/import the `agentc` module namespace.
- Implement `agentc.call(request) -> value`.
- Convert normalized JSON responses directly into Edict/Listree values.
- Add tracing/debug metadata via `agentc.last()`.

### Phase 3 — Edict-native loop
**Goal:** move orchestration into Edict.
- Author a canonical Edict agent loop module.
- Make Edict decide how to handle reply/tool/memory/program directives.
- Keep program execution disabled by default until policy is implemented.

### Phase 4 — Host unification
**Goal:** make transports interchangeable.
- Refactor `cpp-agent` into a thin Edict host.
- Support shell, socket, pipe, and batch entry modes over the same VM.
- Preserve reconnect semantics for the socket host.

### Phase 5 — Persistence and restore
**Goal:** make the Edict-native agent durable.
- Persist loop state and relevant module configuration in slab state.
- Rehydrate `agentc` runtime handles and curated FFI imports on startup.
- Demonstrate detach/reconnect and shutdown/restart continuity.

### Phase 6 — Controlled execution expansion
**Goal:** safely widen what model outputs may trigger.
- Add policy validation for structured directives.
- Add optional controlled `emit_program` / `agentc.exec` behavior.
- Keep unrestricted FFI/model-driven native access behind an explicit off-by-default switch.

### Phase 7 — Migration / deprecation cleanup
**Goal:** retire the old ownership model.
- Mark the outer C++ agent loop as transitional/legacy.
- Reduce duplicated orchestration logic in C++.
- Keep provider/runtime libraries and host front-ends as reusable infrastructure.

## Demonstration Plan
A complete demo for this architecture should show:
1. Start an Edict host process with persisted slab state.
2. Import/activate the `agentc` module.
3. Submit a prompt through one interface (shell, socket, or pipe).
4. Observe `agentc.call(...)` return normalized Edict data.
5. Let an Edict-side loop consume that data and continue the task.
6. Disconnect the client and reconnect without losing state.
7. Shut down the host, restart it, and restore the same durable Edict state.

## Risks and Mitigations
- **Risk**: raw vendor JSON leaks into long-term interpreter contracts.
  - **Mitigation**: normalize all provider responses before Edict consumption.
- **Risk**: recursive `agentc` usage causes runaway loops or token burn.
  - **Mitigation**: enforce depth, turn, timeout, and token budgets.
- **Risk**: model-generated directives become an accidental remote code execution path.
  - **Mitigation**: start with data-only mode and explicit policy gates.
- **Risk**: host and interpreter responsibilities blur again during refactor.
  - **Mitigation**: keep the runtime library transport-only and Edict policy-owned.

## Recommendation
Adopt this architecture as the primary post-G069 runtime direction. Treat the existing native C++ outer agent loop as a successful enabling prototype, then converge toward an **Edict-native agent loop + importable `agentc` capability + thin interchangeable hosts** model.