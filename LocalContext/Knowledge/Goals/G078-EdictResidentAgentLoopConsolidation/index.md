# Goal: G078 — Edict-Resident Agent Loop Consolidation

**Status**: COMPLETE
**Created**: 2026-05-10  
**Completed**: 2026-06-21

## Objective
Consolidate AgentC's duplicated request-shaping, config, root-schema, and UX-loop logic so the Edict VM becomes the authoritative control plane for the user-facing agent experience.

## Rationale
The current system works, but semantic pieces of an LLM turn are duplicated across shell demos, Edict modules, host bootstrap code, runtime normalization, and provider adapters. That duplication makes it harder to evolve the agent loop cleanly and increases the odds of drift between demos, production, and providers.

## Acceptance Criteria
- [x] Define one canonical Edict-resident schema for agent root, runtime config, and normalized turn requests.
- [x] Reduce the runtime C ABI boundary to a thinner execution/transport layer rather than a secondary request-authoring layer.
- [x] Move the interactive UX loop out of `cpp-agent/main.cpp` and into Edict, leaving the host responsible only for lifecycle, persistence, attach/detach transport, and FFI exposure.
- [x] Eliminate redundant host-side/root-side default model/provider/system-prompt definitions where Edict can be the single authority.
- [x] Preserve provider-specific wire adapters in C++ while removing semantic duplication above them.

## Current Findings
- A new curated launcher path now exists to collapse bootstrap/module boilerplate: `edict.sh` injects `EDICT_PATH`, preloads `agentc_curated.edict`, auto-loads the core AgentC Edict modules, configures the `llm` bootstrap surface, and then hands off to ordinary Edict execution modes.
- `agentc_stateful_loop.edict` and `agentc_agent_root.edict` already demonstrate that Edict can own request assembly and turn-state mutation.
- `cpp-agent/main.cpp` remains as a legacy/host interactive executable, but the curated user-facing loop lives in Edict through `./edict.sh`, `llm.init(...)`, provider objects, and `provider < repl! > / /`; host code is no longer the authority for the curated agent loop semantics.
- `cpp-agent/runtime/core/runtime.cpp` remains the C++ transport/adapter normalization seam for provider calls, but canonical turn/request construction for AgentC roots is in Edict modules (`agentc_provider_contracts.edict`, `agentc_agent_root.edict`, `llm.edict`).
- `cpp-agent/runtime/persistence/agent_root_vm_ops.cpp` now keeps only the host-created root skeleton and rehydrates runtime provider defaults/contracts from the Edict provider catalog instead of mirroring provider contract payloads in C++.
- The first concrete provider-contract slice is now live: `cpp-agent/edict/modules/agentc_provider_contracts.edict` carries contrasting `local` and `google` provider semantics, and `agentc_agent_root.edict` now builds canonical turn requests from `runtime.provider_contract` instead of relying on the older generic state-turn helper.
- Embedded-VM bootstrap/restore no longer owns a C++ `provider_contract_json_for(...)` mirror. Rehydration parses the Edict provider catalog (`agentc_provider_contracts.edict`) as the single provider-contract source, fills `runtime.default_provider`, `runtime.default_model`, and `runtime.provider_contract`, and leaves provider-specific wire adapters in C++.
- The initial attempt to resolve provider contracts dynamically in pure Edict via string equality was invalid because the current Edict surface does not support `==` for this use; explicit `provider_contract` carriage is the working near-term seam.
- The first slice is regression-covered across direct Edict root tests, embedded runtime turns, restore/resume, and session persistence, so the new contract seam is now safe to extend.
- The first `llm.init(...)` slice is now live in `cpp-agent/edict/modules/llm.edict`: Edict can resolve named presets like `local-qwen`, configure bootstrap import paths, and construct stable provider objects that own runtime config, conversation state, and a mutating `request` thunk.
- The local runnable demo has now been moved onto that same seam: `./demo_local_llm.sh` no longer uses the older direct `agentc_state_turn(...)` path and instead exercises `llm.init([local-qwen])` plus provider-scoped `request!` directly.
- The provider-object API was intentionally revised away from returned-provider closure regeneration toward in-place mutation after confirming real VM behavior: isolated call syntax does not naturally mutate sibling fields, while provider-scoped context execution does.
- The current reliable request-driving pattern is `provider < [prompt] request! > / /`; this mutates provider state in place and avoids relying on rebinding/history semantics for whole-object replacement.
- The first launcher-backed provider REPL loop is now live: provider objects expose `repl`, the current invocation pattern is `provider < repl! > / /`, and the loop can consume multiple prompts, skip blank lines, and exit cleanly on EOF through `./edict.sh`.
- The provider request path now supports repeated turns correctly; the earlier stale-second-turn bug came from helper-local stack leakage and from replacing `provider.conversation` with its `messages` list during response application. Both issues are fixed.
- The curated launcher now supports zero-user-code default chat: `./edict.sh` auto-initializes a default provider preset and enters `provider.repl()` unless `EDICT_AUTO_CHAT=0` is set, while `EDICT_DEFAULT_PRESET` selects the default preset.
- Reliable `& |` integration in Edict requires explicit scalar success/failure signals. Raw object truthiness, string booleans like `"false"`, and null-path lookup are not safe branching discriminators on the current VM surface.
- A new agent-oriented reference, 🔗[WP — LLM's Guide to Edict and the VM](../../WorkProducts/WP-LlmsGuideToEdictVm-2026-05-10/index.md), now captures the exact compiler/VM behaviors and quick-start patterns that informed the new provider API.
- The VM now supports configurable unresolved-lookup modes at the `REF` boundary: `lax!` preserves symbolic fallback, `strict!` / `strict_null!` return `null`, and `strict_fail!` returns `null` plus enters failure state for cleaner debugging and `& |`-driven control flow.
- The curated launcher is already being used by the runnable local demo: `demo_local_llm.sh` now delegates to `./edict.sh`, and the launcher-backed prelude leaves provider calls immediately available instead of requiring each script to re-import and re-load the same runtime/module stack.
- Raw Edict now has a user-facing named-session selector from 🔗[G102](../G102-EdictSessionIdStartupFlag/index.md): `./build/edict/edict --session ID` creates/resumes an Edict root scope under `/tmp/session/<id>/` by default, using the current session-image/slab store on normal process exit. This narrows lifecycle/persistence seams for future Edict-resident sessions without completing full kill-mid-turn mmap resume.
- 🔗[G080](../G080-LlmReplContextManagement/index.md) landed the first Edict-owned context-management slice: provider objects expose `context_reset!` and `context_inspect!`, and launcher-backed `provider.repl()` supports `/reset`, `/clear`, `/context`, and `/inspect` without moving the UX policy back into the host.
- 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md) has begun with the first deterministic `intern_run!` worker dispatch primitive: the coordinator can freeze shared context/imports, snapshot explicit input, run bounded Edict source in a fresh worker VM with private workspace, and collect a structured result copied back on the coordinator thread.

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
- first provider-session context management is landed through G080; remaining follow-on context work is trim/summarize policies;
- incomplete live-notebook/FFI documentation for LLM usability.

Near-term execution should continue through G079 first, then G080, while keeping G078 as the parent architectural umbrella.

## Progress Notes
- 2026-06-21: Completed G078. The final consolidation removed the temporary C++ provider-contract mirror from `agent_root_vm_ops.cpp`; runtime rehydration now fills provider defaults/contracts from `agentc_provider_contracts.edict`, while the host-created root skeleton stays allocation-neutral and C++ remains responsible for native transport/persistence/credentials/lifecycle. Added regression coverage proving missing runtime defaults are restored from the Edict catalog. Validation: focused `AgentRootVmOpsTest.*` plus file-backed scheduler/session smoke 8/8, full `cpp_agent_tests` 56/56, full `edict_tests` 186/186, `reflect_tests` 55/55, `listree_tests` 83/83, `cartographer_tests` 52/52, and `treesitter_tests` 28/28.
- 2026-06-20: Completed G094 (curated native cognitive capabilities): tree-sitter AST bridge, structural diff engine, and persistent knowledge graph are now available as Edict builtins (`treesitter.load`/`parse`/`list`/`diff`, `kgraph.create`/`add_node`/`add_edge`/`get_node`/`query`/`nodes`/`edges`). Full `edict_tests` 186/186, `treesitter_tests` 28/28 (15 bridge + 6 diff + 7 KG). Also completed post-G096/G106 control-plane progression through G101/G100: G103 is complete and retired as the static declaration image MVP; G096 covers deterministic root/scheduler/static-mount session resume; G106 covers Root1 logical publication registry and manifest/hash/root validation; G101 adds guarded direct Edict action dispatch; G100 documents/tests the isolated-call safety boundary.
- 2026-05-14: Began G091 with deterministic `intern_run!`: task envelopes now dispatch bounded Edict programs to a worker VM, freeze shared context/imports, snapshot input, return structured results, and have focused `InternWorkerTest.*` coverage.
- 2026-05-14: Completed G080's first LLM REPL context-management slice: provider-owned `context_reset!` and `context_inspect!` are live, the launcher REPL supports `/reset`/`/clear` and `/context`/`/inspect`, and focused cpp-agent Edict/LLM validation passed 21/21.
- 2026-05-14: Completed G102's raw Edict session-id slice: `--session` / `--session-base` create/resume support is wired to `SessionStateStore`, session ids are filesystem-safe, CLI regression coverage exists, and root bindings can persist across raw Edict process invocations.
- 2026-05-11: Completed G074's first streaming slice: runtime stream requests now launch detached provider workers, `agentc_stream_sync!` returns structured sync envelopes, provider objects expose `stream_start`/`stream_sync`, and a live Google/Gemma `gemma-4-31b-it` smoke test returned `ok`.
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
