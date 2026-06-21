# Goal: G088 — Refresh README Current State

**Status**: COMPLETE  
**Created**: 2026-05-13  
**Completed**: 2026-05-13

## Objective
Update `README.md` so it accurately describes AgentC's current architecture, direction, and user-facing entry points.

## Rationale
The README still describes AgentC as a substrate that does not replace the outer tool-driven loop, but current project direction is explicitly Edict-resident control-plane ownership. The README should also reflect the mmap/Listree cognitive-state thesis, Edict/Cartographer/miniKanren composition, live LLM runtime work, and intern-concurrency direction.

## Scope
- README project vision and architecture sections.
- Current build/run/test instructions.
- Current status and known validation caveats.
- Near-term agent/intern roadmap.

## Acceptance Criteria
- [x] README no longer says AgentC is not replacing the outer tool loop.
- [x] README describes Edict as the intended agent control plane with C++ as native runtime substrate.
- [x] README incorporates current unique properties: mmap persistence, O(1) speculation, token-efficient Edict, FFI as cognitive operation, embedded miniKanren.
- [x] README includes current launcher/modules/provider surface and live LLM/runtime status.
- [x] README summarizes intern-concurrency architecture as current direction/future work without overstating what is implemented.
- [x] Basic formatting/link/style check passes.

## Implementation Notes — 2026-05-13
- Rewrote `README.md` around the current Edict-resident agent control-plane direction rather than the older "substrate behind an outer tool loop" framing.
- Added sections for unique AgentC properties, current architecture, Edict quick-reference patterns, curated launcher/provider modules, intern/worker-agent direction, updated directory layout, current build/run commands, and validation caveats.
- Incorporated the intern concurrency model as future direction while explicitly noting the implemented building blocks (`LtvFlags::ReadOnly`, fresh worker VMs/thread POC, shared cells, `StreamManager`, `llm.init([local-qwen])`, and `agentc_tools`).

## Validation — 2026-05-13
- `rg -n "not to replace|outer tool-driven|word !|method !|! !|All 90 tests|Next: \\*\\*embedded" README.md || true` — no stale README phrasing found.
- `git diff --check -- README.md LocalContext/Knowledge/Goals/G088-RefreshReadmeCurrentState/index.md` — passed.
- Python README local-link check — passed.

## Follow-up Correction — 2026-05-13
- Clarified that the README miniKanren logic example is runnable as a script via `./build/edict/edict -`, a file, or `-e`; the interactive REPL compiles one line at a time and does not assemble multi-line object literals pasted at the prompt.
- Switched the README logic example to the tested `membero(q, [tea, cake])` form and documented the expected `["tea","cake"]` JSON output.
- Validation: the README heredoc example printed `["tea","cake"]`; `git diff --check -- README.md` passed.
