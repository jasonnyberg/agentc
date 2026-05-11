# Goal: G078 — Edict-Resident Agent Loop Consolidation

**Status**: ACTIVE  
**Created**: 2026-05-10  
**Reassessed**: 2026-05-11

## Objective
Consolidate AgentC's duplicated request-shaping, config, root-schema, and UX-loop logic so the Edict VM becomes the authoritative control plane for the user-facing agent experience.

## Rationale
The current system works, but semantic pieces of an LLM turn are duplicated across shell demos, Edict modules, host bootstrap code, runtime normalization, and provider adapters. That duplication makes it harder to evolve the agent loop cleanly and increases the odds of drift between demos, production, and providers.

## Acceptance Criteria
- [ ] Define one canonical Edict-resident schema for agent root, runtime config, and normalized turn requests.
- [ ] Reduce the runtime C ABI boundary to a thinner execution/transport layer rather than a secondary request-authoring layer.
- [ ] Move the interactive UX loop out of `cpp-agent/main.cpp` and into Edict, leaving the host responsible only for lifecycle, persistence, attach/detach transport, and FFI exposure.
- [ ] Eliminate redundant host-side/root-side default model/provider/system-prompt definitions where Edict can be the single authority.
- [ ] Preserve provider-specific wire adapters in C++ while removing semantic duplication above them.

## Current Findings
- A new curated launcher path now exists to collapse bootstrap/module boilerplate: `edict.sh` injects `EDICT_PATH`, preloads `agentc_curated.edict`, auto-loads the core AgentC Edict modules, configures the `llm` bootstrap surface, and then hands off to ordinary Edict execution modes.
- `agentc_stateful_loop.edict` and `agentc_agent_root.edict` already demonstrate that Edict can own request assembly and turn-state mutation.
- `cpp-agent/main.cpp` still owns the live UI loop, command handling, and fallback root/config construction.
- `cpp-agent/runtime/core/runtime.cpp` still performs request normalization and fallback resolution that overlaps with the desired Edict control-plane role.
- `cpp-agent/runtime/persistence/agent_root_vm_ops.cpp` still duplicates the root schema in `make_default_agent_root(...)` relative to the Edict root constructors.
- The first concrete provider-contract slice is now live: `cpp-agent/edict/modules/agentc_provider_contracts.edict` carries contrasting `local` and `google` provider semantics, and `agentc_agent_root.edict` now builds canonical turn requests from `runtime.provider_contract` instead of relying on the older generic state-turn helper.
- Embedded-VM bootstrap and restore currently mirror those provider contracts in C++ (`provider_contract_json_for(...)`) as a temporary bridge so host-created and restored roots can participate in the new Edict-native request-building path without dynamic provider lookup inside Edict.
- The initial attempt to resolve provider contracts dynamically in pure Edict via string equality was invalid because the current Edict surface does not support `==` for this use; explicit `provider_contract` carriage is the working near-term seam.
- The first slice is regression-covered across direct Edict root tests, embedded runtime turns, restore/resume, and session persistence, so the new contract seam is now safe to extend.
- The first `llm.init(...)` slice is now live in `cpp-agent/edict/modules/llm.edict`: Edict can resolve named presets like `local-qwen`, configure bootstrap import paths, and construct stable provider objects that own runtime config, conversation state, and a mutating `request` thunk.
- The local runnable demo has now been moved onto that same seam: `./demo_local_llm.sh` no longer uses the older direct `agentc_state_turn(...)` path and instead exercises `llm.init([local-qwen])` plus provider-scoped `request !` directly.
- The provider-object API was intentionally revised away from returned-provider closure regeneration toward in-place mutation after confirming real VM behavior: isolated call syntax does not naturally mutate sibling fields, while provider-scoped context execution does.
- The current reliable request-driving pattern is `provider < [prompt] request ! > pop /`; this mutates provider state in place and avoids relying on rebinding/history semantics for whole-object replacement.
- The first launcher-backed provider REPL loop is now live: provider objects expose `repl`, the current invocation pattern is `provider < repl ! > pop /`, and the loop can consume multiple prompts, skip blank lines, and exit cleanly on EOF through `./edict.sh`.
- The provider request path now supports repeated turns correctly; the earlier stale-second-turn bug came from helper-local stack leakage and from replacing `provider.conversation` with its `messages` list during response application. Both issues are fixed.
- The curated launcher now supports zero-user-code default chat: `./edict.sh` auto-initializes a default provider preset and enters `provider.repl()` unless `EDICT_AUTO_CHAT=0` is set, while `EDICT_DEFAULT_PRESET` selects the default preset.
- Reliable `& |` integration in Edict requires explicit scalar success/failure signals. Raw object truthiness, string booleans like `"false"`, and null-path lookup are not safe branching discriminators on the current VM surface.
- A new agent-oriented reference, 🔗[WP — LLM's Guide to Edict and the VM](../../WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md), now captures the exact compiler/VM behaviors and quick-start patterns that informed the new provider API.
- The VM now supports configurable unresolved-lookup modes at the `REF` boundary: `lax!` preserves symbolic fallback, `strict!` / `strict_null!` return `null`, and `strict_fail!` returns `null` plus enters failure state for cleaner debugging and `& |`-driven control flow.
- The curated launcher is already being used by the runnable local demo: `demo_local_llm.sh` now delegates to `./edict.sh`, and the launcher-backed prelude leaves provider calls immediately available instead of requiring each script to re-import and re-load the same runtime/module stack.

## Primary Direction
Make Edict the source of truth for:
- agent root schema
- runtime config object shape
- normalized request construction
- UX loop state machine
- response projection for display

Keep C++ authoritative only for:
- provider transport
- credentials and native secrets lookup
- provider-specific payload translation
- persistence and process lifecycle
- socket attach/detach transport

## Reassessment — 2026-05-11
G078 remains the top-level active consolidation track, but several implementation slices are now complete enough to retire as separate goals: local demo diagnostics, Codex provider support, prefix-sigil semantics, and tail-prefixed dictionary history. The remaining consolidation debt is narrower:

- temporary C++ mirroring of Edict provider contracts during bootstrap/rehydration;
- host-owned outer UX responsibilities in `cpp-agent/main.cpp` versus the newer launcher-backed `provider.repl()` path;
- first Edict-resident tool/action semantics landed through G079; deeper model-driven tool policy remains future work;
- first decoupled ghost-queue provider streaming surface landed through G074;
- missing provider-session context management, now tracked as G080;
- incomplete live-notebook/FFI documentation for LLM usability.

Near-term execution should continue through G079 first, then G080, while keeping G078 as the parent architectural umbrella.

## Progress Notes
- 2026-05-11: Completed G074's first streaming slice: runtime stream requests now launch detached provider workers, `agentc_stream_sync !` returns structured sync envelopes, provider objects expose `stream_start`/`stream_sync`, and a live Google/Gemma `gemma-4-31b-it` smoke test returned `ok`.
- 2026-05-11: Completed G079's first tool/action slice: Edict now has file read/write/exact-replace and shell wrappers via `agentc_tools`, and provider objects expose them as `provider.tools` for provider-context use.
- 2026-05-11: Reassessed after completing G081/G082/G083 and archiving completed goals. G078 stays active as the parent track; G079 is the immediate next implementation slice, G080 follows, and G074/G075 are deferred.
- 2026-05-10: Landed the first Edict-side provider contract module (`agentc_provider_contracts.edict`) with `local` and `google` as contrasting shapes.
- 2026-05-10: Refactored `agentc_agent_root_turn` so the root module stages the user turn, builds the canonical request from `runtime.provider_contract`, calls the runtime, and reapplies the response into root-owned conversation state.
- 2026-05-10: Updated embedded-VM bootstrap/import artifacts and focused regression suites so the provider-contract seam is exercised through root construction, runtime invocation, persistence, and restore.
- 2026-05-10: Added the first Edict-native `llm` module plus targeted regression coverage, then iterated its design from closure-regenerated returned providers to stable mutable provider objects with in-place request execution.
- 2026-05-10: Added an agent-facing Edict/VM guide grounded in `edict_compiler.cpp`, `edict_vm.cpp`, and direct VM experiments so future implementation can rely on accurate stack/frame/truthiness guidance instead of stale prose.
- 2026-05-10: Added VM-level unresolved-lookup modes plus focused regression tests so Edict code can switch between symbolic fallback (`lax!`), null-returning strict lookup (`strict!` / `strict_null!`), and failure-signaling strict lookup (`strict_fail!`).
- 2026-05-10: Added `agentc_curated.edict` plus the root `edict.sh` launcher so a stable `EDICT_PATH`-driven prelude can auto-load the core AgentC Edict surface and make provider calls available in REPL, `-e`, stdin, and file execution without repeated shell boilerplate.
- 2026-05-10: Added the first provider-side REPL/loop surface plus regression coverage, then debugged it through native stdin-status packaging, isolated recursion, and repeated-turn request-state fixes so launcher-backed multi-turn sessions now work through `provider.repl()`.
- 2026-05-10: Extended `edict.sh` so no-arg launch can auto-create a default provider and drop directly into `provider.repl()` with zero user code, while `EDICT_AUTO_CHAT=0` preserves the curated raw REPL path.
- 2026-05-10: Remaining consolidation debt after these slices is primarily the temporary C++ contract mirroring during bootstrap/rehydration and the still-host-owned outer UX loop responsibilities in `cpp-agent/main.cpp`, now that the first provider-side REPL surface is live.
