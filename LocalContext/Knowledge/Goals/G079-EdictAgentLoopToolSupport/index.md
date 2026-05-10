# Goal: G079 — Edict Agent Loop Tool Support

**Status**: PLANNED  
**Created**: 2026-05-10

## Objective
Add a minimal, useful first tool surface to the Edict-owned agent loop so provider-driven chat sessions can read files, write/edit files, and invoke shell/system commands through explicit Edict/FFI capabilities rather than host-owned special cases.

## Rationale
The new `llm.init(...)`, stable provider objects, curated launcher, and `provider.repl()` loop have established the first viable Edict-resident UX seam. The next capability gap is tool use: without a small but real file/system surface, the loop can chat but cannot yet act as an engineering agent. This work should keep the control plane in Edict while exposing only narrow mechanical capabilities through FFI.

## Acceptance Criteria
- [ ] Expose minimal file-reading capability to Edict/FFI suitable for agent-loop use.
- [ ] Expose minimal file-writing and/or edit capability to Edict/FFI with a narrow, explicit contract.
- [ ] Expose minimal shell/system-command execution to Edict/FFI.
- [ ] Define Edict-side wrapper words and calling patterns so tools are usable directly from provider-driven loops.
- [ ] Demonstrate the first tool-aware provider-loop flow under the curated launcher.

## Initial Scope
- File read
- File write/edit
- Shell/system command execution

## Constraints
- Keep capability boundaries explicit and narrow.
- Prefer Edict-side orchestration and policy over host-owned special cases.
- Preserve the curated launcher / provider-object architecture rather than bypassing it.

## Relationship To G078
G078 establishes the Edict-resident loop and provider surface. G079 extends that loop with its first action/tool capabilities.
