# Goal: G100 — Edict Isolation Contract Hardening

**Status**: COMPLETE
**Created**: 2026-05-14
**Completed**: 2026-06-20
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)

## Objective

Harden and document the Edict call/isolation contract used for LLM-generated code, especially the claim that isolated `f(args)` execution prevents accidental parent-state corruption.

## Source Extracted From

Conversation summary section **“Token-efficient language”**.

## Rationale

The summary frames Edict's compactness and call isolation as a user-visible safety property. If LLMs are expected to emit Edict directly, the isolation boundary needs durable tests, examples, and documentation that distinguish isolated calls from deliberate context mutation (`< ... >`).

## Summary

G100 is complete. The project now has regression coverage and LLM-facing documentation for the Edict isolation boundary:

- isolated `f(args...)` calls cannot overwrite parent bindings through thunk-body `@name` assignments;
- isolated calls cannot leak new thunk-body bindings to the parent dictionary;
- isolated call return values still merge back to the caller stack;
- explicit context entry `ctx < ... > /` still deliberately mutates the target object;
- the LLM guide now includes current-syntax safety examples and compact direct-Edict vs JSON-envelope guidance.

See 📄[WP — G100 Edict Isolation Contract Hardening](../../WorkProducts/WP_G100_EdictIsolationContractHardening.md).

## Implementation Plan

- [x] Audit existing call-isolation tests and docs for coverage of parent-state protection.
- [x] Add missing regression tests for isolated calls, provider/object context mutation, and failure cases.
- [x] Document safe LLM code-generation patterns: when to use isolated calls vs explicit context entry.
- [x] Add at least one compact example comparing Edict and equivalent host-language/tool-call overhead.

## Acceptance Criteria

- [x] Tests prove isolated calls cannot accidentally mutate parent bindings in the covered cases.
  - Evidence: `CallIsolationTest.IsolatedCallCannotOverwriteParentBindingFromThunkBody` and `CallIsolationTest.IsolatedCallCannotLeakNewBindingFromThunkBody`.
- [x] Tests prove explicit `< ... >` context mutation still works when requested.
  - Evidence: `CallIsolationTest.ExplicitContextEntryMutatesTargetObject`.
- [x] LLM-facing docs explain the safety boundary without overstating it.
  - Evidence: `WP-LlmsGuideToEdictVm-2026-05-10/index.md` now documents isolated-call safety, explicit context mutation, and direct-action compactness limits.
- [x] Examples use current syntax (`word!`, `/`, `/ /`).
  - Evidence: updated guide examples use `agentc_direct_action!`, `request!`, `repl!`, bare `/`, and `provider < ... > / /`.

## Validation

Passed:

- `cmake --build build --target edict_tests -j$(nproc)`
- `./edict/edict_tests --gtest_filter='CallIsolationTest.*'` — 8/8 passed.
- `git diff --check`

Broader suite note:

- Full `./edict/edict_tests` currently reports 5 unrelated failures in ReproFFI/Callback suites: `ReproFFITest.AddPoC`, `CallbackTest.ClosureFromParamSignature`, `CallbackTest.EdictBuiltinsMapLoadAndInvokeCartographerFunction`, `CallbackTest.ParserMapCanRoundTripThroughJson`, and `CallbackTest.PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren`.
- Do not record full `edict_tests` as green until those unrelated failures are fixed.

## Relationship To Other Goals

- Parent: G078 — Edict-Resident Agent Loop Consolidation.
- Complements: G101 — Direct Edict Tool-Emission Path.
- Next backlog goal after G100: G094 — Curated Native Cognitive Capability Libraries.
