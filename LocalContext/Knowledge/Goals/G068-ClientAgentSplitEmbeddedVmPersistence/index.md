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

Phase 2 has now started: a first `libagent_runtime.so` target exists under `cpp-agent/`, exposes the new C ABI header, registers built-in providers once, and routes JSON requests through a reusable runtime wrapper while the host executable continues to work against the same provider implementations during transition.

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

Phase 5 progress: `cpp-agent/main.cpp` no longer wires providers directly through the old outer loop; it now consumes `libagent_runtime` via the C ABI, maintains lightweight session transcript state, supports `shutdown-agent`, and accepts runtime config via JSON file or defaults.

### Phase 6 - Persistence and Restore
- [ ] Add host-managed checkpoint/save hooks.
- [ ] Add startup restore hooks.
- [ ] Rehydrate transient runtime handles/imports safely without introducing a Host↔VM IPC boundary.

### Phase 7 - Demonstration and Validation
- [ ] Demonstrate a live provider-backed Edict-native session.
- [ ] Demonstrate disconnect/reconnect without losing state.
- [ ] Demonstrate shutdown and restart with VM state restored from persisted slab.
- [ ] Demonstrate the same core runtime via at least two front-end shapes.

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

## Next Action
Add automated tests for the runtime C ABI plus the Edict-side wrapper path, then continue physically migrating common/provider code into the `cpp-agent/runtime/` layout.