# Dashboard

**Project**: AgentC / J3  
**Last Updated**: 2026-05-10

## Open Goals
### Planned / Active
- 🔗[G078 — Edict-Resident Agent Loop Consolidation](./Knowledge/Goals/G078-EdictResidentAgentLoopConsolidation/index.md) — **IN PROGRESS**
- 🔗[G079 — Edict Agent Loop Tool Support](./Knowledge/Goals/G079-EdictAgentLoopToolSupport/index.md) — PLANNED
- 🔗[G080 — LLM REPL Context Management](./Knowledge/Goals/G080-LlmReplContextManagement/index.md) — PLANNED
- 🔗[G074 — Real-time FFI Token Streaming](./Knowledge/Goals/G074-RealtimeFFITokenStreaming/index.md) — PLANNED
- 🔗[G075 — Speculative Edict Native Architectures](./Knowledge/Goals/G075-SpeculativeEdictArchitectures/index.md) — PLANNED

### Complete (this cycle)
- 🔗[G077 — Local LLM Demo Request Diagnostics](./Knowledge/Goals/G077-LocalLlmDemoRequestDiagnostics/index.md) — **COMPLETE** (2026-05-10)
- 🔗[G076 — Generalized Multiline Edict Accumulator](./Knowledge/Goals/G076-GeneralizedMultilineAccumulator/index.md) — **COMPLETE** (2026-05-09)
- 🔗[G073 — Pure VM Lifecycle & Host Thinning](./Knowledge/Goals/G073-PureVMLifecycleHostThinning/index.md) — **COMPLETE** (2026-05-06)
- 🔗[G072 — Direct Slab Restore Without Full Library Re-import](./Knowledge/Goals/G072-DirectSlabRestoreWithoutReimport/index.md) — **COMPLETE** (2026-05-06)

### Blocked
None

## Active Context
- **G074 Streaming Architecture Locked**: We have designed the "Decoupled Ghost Queue" approach. Background LLM network streams will write to standard C++ `std::queue`s (ephemeral, disposable RAM). The Edict VM's main thread will explicitly pull from these queues via `agentc_stream_sync !` to mutate its persistent Listree mailboxes. This guarantees Listree mmap safety and deterministic snapshot resilience.
- **Resilience**: If the VM restarts, background C++ threads die. The VM handles stuck mailboxes using native timeouts.
- **Local Runtime Demo Fixed**: `./demo_local_llm.sh` now works against the local OpenAI-compatible server after removing the 30-second SSE timeout, adding outbound request diagnostics, teaching request normalization to accept `messages[*].content`, and replacing the older stateful-loop path with the new `llm.init([local-qwen])` plus provider-scoped `request !` pattern.
- **New High-Priority Consolidation Track**: Request/config/root/UI-loop semantics are currently split across shell demos, Edict modules, host bootstrap, runtime normalization, and provider adapters. The desired direction is to make Edict the authoritative control plane and reduce the runtime/host to thinner execution and lifecycle layers. See 🔗[WP — Edict-Resident Agent Loop Consolidation Plan](./Knowledge/WorkProducts/WP-EdictResidentAgentLoopConsolidation-2026-05-10/index.md).
- **First Provider-Contract Slice Landed**: `agentc_provider_contracts.edict` now carries contrasting `local` and `google` provider semantics in Edict data, `agentc_agent_root.edict` builds canonical turn requests from `runtime.provider_contract`, embedded-VM bootstrap imports the new module, and targeted regression coverage across root-building, embedded turns, restore, and session persistence is passing.
- **Bootstrap Direction Refined**: `llm.init(name)` should become the Edict-owned entrypoint for lazy LLM bootstrap, including importing required runtime/extension libraries if absent, resolving a named provider preset such as `local-qwen`, and returning a provider object that scripts can drive explicitly via methods like `provider.request(...)` and `provider.repl()`.
- **First `llm.init(...)` Slice Landed**: `cpp-agent/edict/modules/llm.edict` now exposes named presets such as `local-qwen`, bootstrap override configuration, and a stable provider object with an in-place mutating `request` thunk. The working Edict-side invocation pattern is `provider < [prompt] request ! > pop /`, which updates provider conversation state and assistant text without rebinding the whole object.
- **Agent-Oriented Edict Guide Added**: 🔗[WP — LLM's Guide to Edict and the VM](./Knowledge/WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md) now records the compiler/VM-grounded mental model, truthiness traps, context-frame behavior, assignment history, and quick-start mutation/control-flow patterns discovered during G078.
- **Strict/Lax Lookup Modes Added**: Edict now has VM-level unresolved lookup modes: `lax!` preserves the old symbolic fallback, `strict!` / `strict_null!` return `null`, and `strict_fail!` returns `null` while entering failure state. This gives Edict code a practical debugging surface without breaking the older metaprogramming behavior by default.
- **Curated Launcher Landed**: `edict.sh` now injects a stable `EDICT_PATH` object, preloads `agentc_curated.edict`, auto-loads the core AgentC Edict modules, configures `llm` bootstrap paths, and then hands off to REPL, `-e`, stdin, or file execution so provider calls are available without repeating import boilerplate.
- **First Provider REPL Landed**: `cpp-agent/edict/modules/llm.edict` now exposes `provider.repl` for launcher-backed chat sessions. The working invocation is `provider < repl ! > pop /`, stdin/status packaging now lives partly in `agentc_stdlib`, and focused coverage proves repeated turns plus clean EOF through `./edict.sh` with the mock runtime.
- **Zero-Code Chat Launcher Landed**: `./edict.sh` now defaults to `EDICT_AUTO_CHAT=1` and `EDICT_DEFAULT_PRESET=local-qwen`, so no-arg launch auto-creates a provider and drops straight into `provider.repl()`. Setting `EDICT_AUTO_CHAT=0` preserves the curated raw REPL path.
- **Next Big Tracks Formalized**: the next major execution tracks after the current G078 seam are 🔗[G079 — Edict Agent Loop Tool Support](./Knowledge/Goals/G079-EdictAgentLoopToolSupport/index.md) and 🔗[G080 — LLM REPL Context Management](./Knowledge/Goals/G080-LlmReplContextManagement/index.md).

## Knowledge Inventory
- **Category Indexes**: `Knowledge/Concepts/index.md`; `Knowledge/Facts/index.md`; `Knowledge/Procedures/index.md`

## Handoff Note

**Project**: AgentC
**Current State**: G078 now has the first Edict provider-contract slice, the first working `llm.init(...)` slice, VM-level strict/lax unresolved lookup modes, a curated launcher/preload path, the first launcher-backed `provider.repl()` loop, and zero-code default chat through `./edict.sh`. Named presets initialize stable provider objects, repeated provider requests now preserve full multi-turn conversation state, `provider.repl()` can consume multiple prompts and exit cleanly on EOF, and targeted coverage across the Edict/LLM/root/embedded/session plus Edict VM suites is passing.
**Next Action**: Start G079 by adding the first minimal Edict/FFI tool surface for file read/write-edit and shell/system execution, while keeping G080 queued immediately behind it for explicit context management inside the provider REPL.
**Key Context**: The current `llm` provider API is intentionally VM-grounded: provider objects are stable, `request !` mutates them in place, `provider < repl ! > pop /` is the current explicit chat-loop pattern, and plain `./edict.sh` now auto-creates a default provider chat session unless `EDICT_AUTO_CHAT=0` is set. `& |` integration should use explicit success/failure sentinels rather than object truthiness, Edict now supports `lax!`, `strict!` / `strict_null!`, and `strict_fail!`, and `edict.sh` / `agentc_curated.edict` is the canonical way to avoid repeated import/bootstrap boilerplate.

## Session Compliance
- [x] Reviewed Dashboard at session start
- [x] Created/updated Goal files for multi-step architectural work
- [x] Persisted durable knowledge needed for future continuation
- [x] Updated Dashboard with current focus and next action
- [x] Left a handoff note for the next session
