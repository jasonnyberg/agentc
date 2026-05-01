# Goal: G068 - Client/Agent Split with Embedded Persistent Edict VM

## Goal
Establish the next-stage AgentC runtime architecture with a reconnectable client/host boundary, an in-process embedded Edict VM, memory-mapped slab persistence for durable state, and an **Edict-native agent loop** exposed through an importable `agentc` capability/module rather than a hardcoded outer C++ orchestration loop.

## Status
**IN PROGRESS**

## Rationale
Recent work proved several important things:
- keeping the interactive client separate from the long-lived runtime process is valuable,
- keeping the Edict VM embedded in that process avoids unnecessary IPC,
- slab-backed persistence is the right durability direction,
- and native provider integration is viable in-process.

The new architectural refinement is that the long-lived host process should **not** remain the owner of the canonical agent loop. Instead, the host should embed Edict and expose interchangeable front-ends (shell, socket, pipe, batch), while LLM transport/provider logic lives behind a reusable native runtime library and appears inside Edict as an importable `agentc` module.

This keeps the previously chosen process boundary:
- keep **Client ↔ Host** as a boundary,
- keep **Host + Edict VM** in one process,
- use **memory-mapped slab persistence** for durable logical state,
- rehydrate runtime-native artifacts after restore,

while also inverting control so that:
- **Edict owns agent policy and orchestration**, and
- **native code owns transport/provider mechanics**.

## Architectural Decision

### Chosen Split
```text
[Shell | Socket | Pipe | File | Batch Front-End]
                  ⇅ stream / request input
        [Host Process / Interpreter Front-End]
                  |
                  | in-process API
                  v
             [Embedded Edict VM]
               - durable cognitive state
               - canonical agent loop
               - tool / memory / logic policy
               - importable `agentc` module
                  |
                  | native runtime bridge
                  v
            [libagent_runtime.so]
               - provider selection
               - credentials
               - HTTP transport
               - response normalization
                  |
                  v
          [Gemini / OpenAI / Copilot / future providers]
```

### Explicit Non-Goals
- Do **not** reintroduce an Agent↔VM IPC split.
- Do **not** make arbitrary model-driven FFI/native access the initial default.
- Do **not** make vendor-specific response JSON the long-term interpreter contract.

## Roles and Communication Boundaries

### Client / Front-End Layer
**Role**
- user-facing shell, socket, pipe, or file/batch attachment,
- send prompts and control commands,
- render streamed or buffered replies,
- disconnect and reconnect safely where transport supports it.

**Communication**
- Unix domain socket initially,
- stream I/O for shell/pipe modes,
- future transport-specific adapters without changing core agent policy.

### Host Process
**Role**
- long-lived runtime shell/daemon,
- owns session acceptance and transport plumbing,
- owns startup/shutdown lifecycle,
- embeds Edict VM,
- rehydrates transient native runtime state,
- exposes/imports the `agentc` capability.

**Communication**
- upward: stream I/O over socket/stdio/file entrypoints,
- inward: direct in-process API boundary to the Edict VM,
- outward: provider HTTP traffic via the native runtime library.

### Embedded Edict VM
**Role**
- durable cognitive state,
- canonical agent loop/orchestration,
- tool/memory/logic policy,
- interpreter-visible request/response handling,
- transaction/rewrite/import facilities,
- persistence-safe runtime configuration.

**Communication**
- direct in-process API boundary only,
- no VM socket bridge on the hot path,
- receives normalized LLM responses as Edict/Listree data.

### Native `agentc` Runtime Library
**Role**
- provider selection,
- credentials/auth,
- HTTP requests,
- optional streaming assembly,
- normalization of provider responses into a single JSON contract.

**Communication**
- callable from the host/VM bridge,
- returns normalized JSON buffers for Edict consumption,
- does not own multi-step agent behavior.

## Durable vs Transient State Boundary

### Durable State (persist in slab)
- mmap-backed Listree/object graph state,
- VM roots and anchors,
- agent memory/goals/facts/conversation state,
- agent-loop state and policy configuration,
- model/provider defaults,
- resumable workflow/task state,
- metadata needed to rehydrate curated imports and runtime configuration.

### Transient Rehydrated State (rebuild on restore)
- FFI dynamic-library handles,
- function pointers and imported runtime binding artifacts,
- open sockets,
- active HTTP streams,
- provider client state,
- SSE/parser buffers,
- in-flight request state,
- thread-local runtime state.

## Module / Capability Model

### Initial required surface
- `agentc.call(request) -> value`
  - Dispatch prompt/request to the selected model.
  - Normalize provider response to JSON.
  - Convert JSON directly into Edict/Listree.
  - Return data to Edict policy code.
- `agentc.configure(config)`
  - Set default provider/model/policy values.
- `agentc.last()`
  - Return debugging/trace metadata for the last call.

### Initial execution rule
Model output is **data first**. The first production architecture does not grant the model unrestricted access to interpreter-native FFI/import powers. Any later widening of that capability must be behind explicit policy switches.

## Lifecycle Semantics
- `exit` → disconnect current client/front-end session only.
- `shutdown-agent` → checkpoint slab state, quiesce host activity, terminate the long-lived host process.
- host startup → restore VM state from slab when available, rehydrate runtime/native capabilities, then begin accepting sessions.
- client restart → reconnect to the same background host without requiring VM teardown.

## Dependencies / Related Goals
- Builds on prior persistence groundwork:
  - 🔗[G041 Persistent Slab Image Persistence](../G041_Persistent_Slab_Image_Persistence/index.md)
  - 🔗[G042 Persistent VM Root State and Restore Validation](../G042_Persistent_VM_Root_State_And_Restore_Validation/index.md)
- Aligns with interactive runtime work:
  - 🔗[G063 Native C++ Agent Core](../G063-NativeCppAgentCore/index.md)
  - 🔗[G066 Socket-Enabled Agent](../G066-SocketEnabledAgent/index.md)
  - 🔗[G067 Atomic AgentC Agent](../G067-AtomicAgentcAgent/index.md)
- Depends on build/persistence simplification work:
  - 🔗[G069 Remove LMDB Dependencies](../G069-RemoveLMDBDependencies/index.md)
- Detailed architecture plan:
  - 🔗[WP_EdictNativeAgentModuleArchitecture](../../WorkProducts/WP_EdictNativeAgentModuleArchitecture.md)
  - 🔗[WP_EmbeddedPersistentAgentArchitecture](../../WorkProducts/WP_EmbeddedPersistentAgentArchitecture.md)

## Acceptance Criteria
- [x] The architecture is documented as **Client/Host split, Host+VM embedded** with no Host↔VM IPC dependency.
- [x] A concrete work product defines the **Edict-native loop** and **importable `agentc` module** direction.
- [ ] The long-lived host process can survive client disconnects and accept reconnects over the socket interface.
- [x] VM persistence design explicitly uses a memory-mapped slab image rather than LMDB as the primary restart mechanism.
- [x] Durable vs transient runtime state is documented, including explicit treatment of FFI/runtime artifacts as rehydrated state.
- [x] Lifecycle commands are defined for disconnect vs full agent shutdown.
- [x] Unsafe model-driven native/FFI access is explicitly gated off by default in the target design.
- [x] The architecture supports multiple interchangeable front-ends (socket, pipe, file, shell) over the same embedded interpreter model.
- [ ] A concrete demo plan exists for: start host → interact → disconnect/reconnect → shutdown → restore from persisted slab.

## Implementation Plan

### Phase 1 - Architecture and Runtime Contract
- [x] Capture the chosen architecture in a work product.
- [x] Define the role split between host, embedded VM, and native runtime library.
- [x] Define the initial `agentc.call(request) -> value` capability contract.
- [x] Write the concrete C ABI and normalized request/response schema for the runtime library.

### Phase 2 - Runtime Extraction
- [ ] Extract provider/auth/HTTP code into a reusable native runtime library.
- [ ] Normalize provider responses into a single JSON contract.
- [ ] Preserve current provider support (Gemini/OpenAI/Copilot) behind the new boundary.

Phase 2 has now started: a first `libagent_runtime.so` target exists under `cpp-agent/`, exposes the new C ABI header, registers built-in providers once, and routes JSON requests through a reusable runtime wrapper while the host executable continues to work against the same provider implementations during transition. Common/provider code migration is also underway: the active `agent_runtime` build now consumes `cpp-agent/runtime/common/{credentials,http_client,sse_parser}.*`, `cpp-agent/runtime/core/provider_registry.*`, and `cpp-agent/runtime/providers/{google,openai}/...` rather than the older top-level source locations.

### Phase 3 - Edict Module Surface
- [x] Add/import the first `agentc` wrapper surface over the runtime C ABI.
- [x] Convert normalized JSON responses directly into Edict/Listree values.
- [x] Add debug/trace state wrappers for last-error / last-trace retrieval.

Phase 3 evidence: `cpp-agent/edict/modules/agentc.edict` now wraps `libagent_runtime.so` through Cartographer imports plus `extensions/libagentc_extensions.so`, and `cpp-agent/demo/demo_agentc_runtime_edict.sh` demonstrates the Edict-side path end-to-end.

### Phase 4 - Edict-Native Loop and Policy
- [ ] Move canonical agent orchestration into Edict code/modules.
- [ ] Keep model output data-first initially.
- [ ] Add explicit policy gates before any model-directed program execution or privileged capability use.

### Phase 5 - Host Unification and Lifecycle
- [ ] Refactor `cpp-agent` into a thin host/front-end around the embedded interpreter.
- [x] Preserve reconnectable socket behavior.
- [ ] Support additional front-end modes (shell/pipe/file) without changing agent policy ownership.

Phase 5 progress: `cpp-agent/main.cpp` no longer wires providers directly through the old outer loop; it now consumes `libagent_runtime` via the C ABI, maintains lightweight session transcript state, supports `shutdown-agent`, `reset-session`, and accepts runtime config via JSON file or defaults.

### Phase 6 - Persistence and Restore
- [x] Add host-managed checkpoint/save hooks.
- [x] Add startup restore hooks.
- [ ] Rehydrate transient runtime handles/imports safely without introducing a Host↔VM IPC boundary.

Phase 6 progress: the runtime-backed host now uses `cpp-agent/runtime/persistence/session_state_store.*` to save and restore shared session state through the existing Listree/slab-backed file store path, and the state store is covered by automated tests.

### Phase 7 - Demonstration and Validation
- [x] Demonstrate a live provider-backed Edict-native session.
- [ ] Demonstrate disconnect/reconnect without losing state.
- [ ] Demonstrate shutdown and restart with VM state restored from persisted slab.
- [x] Demonstrate the same core runtime via at least two front-end shapes.

## Risks and Mitigations
- **Risk**: raw vendor JSON becomes an unstable interpreter contract.
  - **Mitigation**: normalize provider responses before Edict consumes them.
- **Risk**: model-directed recursion causes runaway loops or token burn.
  - **Mitigation**: enforce depth, turn, timeout, and token budgets.
- **Risk**: model-emitted directives become an accidental unrestricted execution path.
  - **Mitigation**: start with data-only mode and explicit capability gates.
- **Risk**: the host re-accumulates agent policy during refactor.
  - **Mitigation**: keep native code responsible for transport/runtime services, not orchestration.

## Progress Notes

### 2026-04-30
- Did: Reframed the target runtime around an Edict-native agent loop and documented the architecture in 🔗[WP_EdictNativeAgentModuleArchitecture](../../WorkProducts/WP_EdictNativeAgentModuleArchitecture.md).
- Decided: The canonical LLM interface should be an importable `agentc` Edict module/capability backed by a reusable native runtime library, while the long-lived host keeps transport/lifecycle duties and the VM owns orchestration/policy.
- Remaining: Define the concrete runtime ABI, normalized request/response contract, extraction map from current `cpp-agent` code, and the first Edict module/builtin surface.
- Next: Write the concrete `libagent_runtime` C ABI plus the normalized `agentc.call` request/response schema.

### 2026-04-30 (implementation start)
- Did: Added the first implementation slice for the new runtime boundary: `cpp-agent/include/agentc_runtime/agentc_runtime.h`, runtime core/c_api scaffolding, built-in provider registration, and a new `agent_runtime` shared-library target producing `libagent_runtime.so`.
- Did: Implemented JSON-configured runtime creation/configuration/request methods, normalized JSON success/error envelopes, and a smoke-tested C ABI path via `ctypes` against `libagent_runtime.so`.
- Did: Tightened the OpenAI-compatible provider path so it resolves auth more cleanly and produces a usable final assistant message for normalization.
- Remaining: continue extracting common/provider code into the new runtime layout, add Edict-side wrappers/import path, and move host entrypoints onto the new runtime contract.
- Next: refactor the current host/front-end to consume `libagent_runtime` through the new runtime core rather than owning provider wiring directly.

### 2026-04-30 (host + Edict bridge slice)
- Did: Refactored `cpp-agent/main.cpp` into a thinner runtime-backed host that uses the new C ABI instead of directly wiring providers/agent-loop ownership, while preserving reconnectable socket behavior and adding `shutdown-agent` support.
- Did: Added `cpp-agent/edict/modules/agentc.edict` as the first Edict-side wrapper/import path over `libagent_runtime.so`, using the existing extensions bridge to convert Edict values ↔ JSON strings ↔ C strings.
- Did: Added `cpp-agent/demo/demo_agentc_runtime_edict.sh` and validated the Edict-side wrapper path end-to-end; also validated the runtime-backed socket host with a live Gemini request plus clean shutdown.
- Did: Added `cpp-agent/config/runtime.default.json` and host CLI options (`--config`, `--socket`, `--provider`, `--model`, `--system-prompt`) so runtime selection/config is now externalizable via JSON file or flags.
- Remaining: keep thinning the host, add automated runtime/Edict wrapper tests, and continue moving common/provider code into the new runtime-oriented layout.
- Next: add automated tests for the C ABI + Edict wrapper path, then continue physically migrating provider/common code into `cpp-agent/runtime/`.

### 2026-04-30 (tests + runtime layout migration)
- Did: Added `cpp_agent_tests` with automated coverage for both the raw runtime C ABI and the Edict-side wrapper/module path, and registered it with CTest.
- Did: Validated the new tests via both direct execution and `ctest -R cpp_agent_tests`; coverage now proves runtime creation/configuration error reporting plus the Cartographer-imported `agentc.edict` wrapper round-trip.
- Did: Continued physical source-layout migration by moving the active `agent_runtime` build onto `cpp-agent/runtime/common/{credentials,http_client,sse_parser}.*` and `cpp-agent/runtime/providers/{google,openai}/...` while preserving working host/runtime behavior.
- Remaining: continue shrinking transitional top-level `cpp-agent` sources, migrate additional runtime-facing code/helpers into the new layout, and start reconnecting the host to embedded VM persistence/restore hooks.
- Next: move the remaining reusable runtime support code and transitional compatibility shims toward the `cpp-agent/runtime/` layout, then begin stitching the runtime-backed host into the mmap/Listree persistence lifecycle.

### 2026-04-30 (persistence hook-up slice)
- Did: Added `cpp-agent/runtime/persistence/session_state_store.*` to persist host session state through the existing Listree/slab-backed file-store path and covered it with `SessionStateStoreTest`.
- Did: Updated the runtime-backed host to restore prior session state on startup, persist shared conversation state after each successful turn, and support `reset-session` to clear both in-memory and persisted state.
- Did: Manually validated restart continuity with a live Gemini-backed session using `--state-base`: first host run answered a prompt, second host run restored session state and answered a follow-up grounded in the prior turn.
- Remaining: move from persisted transcript state toward persisted embedded-VM/runtime state, and connect transient runtime rehydration to the host startup lifecycle more explicitly.
- Next: continue migrating remaining reusable runtime support code while designing the next persistence step from session-transcript restore toward embedded VM/root-anchor restore ownership in the host.

### 2026-04-30 (runtime support migration continuation)
- Did: Introduced `cpp-agent/runtime/core/provider_registry.*` and moved the active runtime build onto the new registry boundary instead of the old top-level `api_registry.cpp` implementation.
- Did: Reduced legacy top-level runtime-support files (`api_registry.*`, `credentials.*`, `providers/google.*`, `providers/openai.*`) to compatibility wrappers/shims that forward toward the new runtime-oriented implementations.
- Did: Added final top-level compatibility shims for `http_client.*` and `sse_parser.*`, so older include paths now forward into `cpp-agent/runtime/common/...` instead of duplicating behavior.
- Did: Revalidated the runtime-backed host, `cpp_agent_tests`, and `ctest -R cpp_agent_tests` after the support-code migration.
- Remaining: continue shrinking or relocating remaining transitional top-level runtime-support sources and move the persistence boundary closer to embedded VM/root-anchor ownership.
- Next: migrate any remaining reusable runtime helpers off transitional top-level locations, then plan the first host-owned embedded VM/root restore slice.

### 2026-04-30 (Edict live LLM import verification)
- Did: Verified directly that Edict can import `libagent_runtime.so` through Cartographer, create a runtime handle, dispatch a live Google/Gemini request, and receive a normalized response object back through the `agentc_call` wrapper path.
- Did: Produced a minimal hello-world-style inverted-loop primitive by running an Edict script that imported the runtime library and returned a live assistant message (`"Hello world"`) from Gemini through the data-first JSON→Edict path.
- Decided: This is sufficient evidence that the core inversion seam now works — Edict can directly own the LLM request primitive even though the full canonical Edict-owned multi-step loop is not implemented yet.
- Remaining: Build the first tiny Edict-owned loop/module on top of this verified primitive, then continue pushing persistence deeper than transcript-level state.
- Next: Implement a minimal Edict-owned hello-world loop module that accepts a prompt, calls the runtime, extracts assistant text, and returns it as the canonical inverted-loop POC.

### 2026-04-30 (hello-world inverted loop POC)
- Did: Added `cpp-agent/edict/modules/agentc_hello_loop.edict`, a tiny Edict-owned loop primitive that builds a request object, calls `agentc_call`, extracts `response.message.text`, and returns assistant text.
- Did: Added `cpp-agent/demo/demo_inverted_hello_loop.sh` and validated a live Google/Gemini run where Edict itself owned the request/response step and printed `Hello world`.
- Did: Added `EdictHelloLoopTest` to `cpp_agent_tests`, proving the hello-loop helper module loads and extracts assistant text from the normalized response shape.
- Decided: This is the first concrete POC of the inverted loop architecture: the outer host no longer needs to own the single-step LLM turn in order for Edict to request and interpret a model response.
- Remaining: Grow this from a hello-world single-turn primitive into a small stateful Edict-owned loop, then keep moving persistence from transcript-level host state toward embedded VM/root-anchor ownership.
- Next: Extend the hello-loop primitive into a slightly richer Edict-owned loop that can accept/store conversation state explicitly rather than only returning a single assistant text.

### 2026-04-30 (stateful Edict loop slice)
- Did: Added `cpp-agent/edict/modules/agentc_stateful_loop.edict`, which introduces a slightly richer Edict-owned turn contract with `agentc_state_init`, `agentc_state_turn`, and structured state fields including `system_prompt`, `last_prompt`, `last_response`, and `assistant_text`.
- Did: Added `cpp-agent/demo/demo_inverted_stateful_loop.sh` and validated a live Google/Gemini run where Edict returned a structured turn-state object containing both assistant text and the normalized last response.
- Did: Added `EdictStatefulLoopTest` to `cpp_agent_tests`, proving the stateful loop helper builds the expected structured state shape.
- Decided: The project now has a concrete next-step POC beyond hello-world — Edict can own a structured single-turn state transition, not just a bare assistant string.
- Remaining: Extend the state shape to include explicit conversation history or loop-owned memory, then start shifting durable authority from host transcript persistence toward VM/root-owned state.
- Next: Extend the stateful Edict loop to carry explicit conversation/history state across turns in Edict-owned data, then align that with deeper VM/root persistence.

## Next Action
Extend the stateful Edict loop to carry explicit conversation/history state across turns in Edict-owned data, then continue migrating remaining runtime helpers and push persistence beyond transcript-level state toward host-owned embedded VM/root restore.