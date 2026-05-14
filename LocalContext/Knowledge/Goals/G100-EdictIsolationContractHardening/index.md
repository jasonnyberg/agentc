# Goal: G100 — Edict Isolation Contract Hardening

**Status**: PLANNED  
**Created**: 2026-05-14

## Objective
Harden and document the Edict call/isolation contract used for LLM-generated code, especially the claim that isolated `f(args)` execution prevents accidental parent-state corruption.

## Source Extracted From
Conversation summary section **“Token-efficient language”**.

## Rationale
The summary frames Edict's compactness and call isolation as a user-visible safety property. If LLMs are expected to emit Edict directly, the isolation boundary needs durable tests, examples, and documentation that distinguish isolated calls from deliberate context mutation (`< ... >`).

## Implementation Plan
- [ ] Audit existing call-isolation tests and docs for coverage of parent-state protection.
- [ ] Add missing regression tests for isolated calls, provider/object context mutation, and failure cases.
- [ ] Document safe LLM code-generation patterns: when to use isolated calls vs explicit context entry.
- [ ] Add at least one compact example comparing Edict and equivalent host-language/tool-call overhead.

## Acceptance Criteria
- [ ] Tests prove isolated calls cannot accidentally mutate parent bindings in the covered cases.
- [ ] Tests prove explicit `< ... >` context mutation still works when requested.
- [ ] LLM-facing docs explain the safety boundary without overstating it.
- [ ] Examples use current syntax (`word!`, `/`, `/ /`).
