# G052 - Runtime Boundary Hardening

## Status: IN PROGRESS

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Supersedes runtime carryover from 🔗[`G049-EdictVMMultithreading`](../G049-EdictVMMultithreading/index.md)
- Incorporates conclusions from 🔗[`G051-CursorVisitedReadOnlyBoundary`](../G051-CursorVisitedReadOnlyBoundary/index.md)
- Explicitly stays narrower than 🔗[`G053-SharedRootFineGrainedMultithreading`](../G053-SharedRootFineGrainedMultithreading/index.md)

## Goal

Consolidate the remaining runtime follow-through work into one hardening goal: keep AgentC's first-slice multithreading boundary explicit, well-tested, and difficult to misuse without reopening broad shared-state or alternate concurrency models.

## Why This Matters

- G049's first threading slice is now implemented and verified, but the remaining work is cleanup/hardening rather than feature expansion.
- G051 established that cursor-visited read-only marking should not become the main concurrency boundary, so the project now needs one consolidated place to track the accepted boundary model and its remaining guardrails.
- A new future-looking goal (G053) now tracks investigation of truly shared-root / fine-grained Listree sharing, so G052 can stay focused on preserving the current conservative model rather than drifting into premature architecture expansion.

## Consolidated Carryover

### From G049

- Continue validating that supported mutable cross-thread sharing stays behind protected shared-value cells.
- Add small misuse-detection guards where unsupported live-object transfer cases can be rejected cheaply and safely.
- Keep the pthread helper surface minimal and coherent.

### From G051

- Preserve the decision that cursor-visited read-only marking is not the primary concurrency model.
- Reuse G051 only as a source of rejected alternatives and future debug-only ideas.

## Current Accepted Boundary Model

- Threaded Edict callbacks run in a fresh `EdictVM` per worker.
- Mutable cross-thread coordination happens only through protected shared-value cells.
- Arbitrary live `ListreeValue` graph sharing remains unsupported.
- Iterator/cursor-valued `ltv` handles are explicitly rejected at the helper transfer boundary.
- Shared-root / fine-grained shared-Listree multithreading is deferred to G053 and is not part of G052's supported surface.

## Near-Term Checklist

- [x] Audit for any additional concrete non-transferable live-object cases worth rejecting at the helper boundary.
- [x] Decide whether G049's remaining unchecked validation item can now be closed under the consolidated boundary model.
- [ ] Align dashboard/recent-goal summaries so the active runtime story points at this goal rather than the retired subgoals.
- [ ] Keep full-suite verification green while boundary hardening changes land.
- [ ] Keep G052 scoped to hardening the current conservative threading model rather than informally expanding toward G053.

## Current Audit Readout

- The current helper-boundary audit suggests `LtvFlags::Iterator` is the only concrete `ListreeValue` flag that presently denotes a live runtime object rather than ordinary value/storage semantics.
- `Immediate`, `SlabBlob`, `StaticView`, `Binary`, `List`, and `LogicVar` currently describe storage mode or term semantics, not a live cross-thread object that needs the same kind of rejection path.
- Current evidence therefore supports keeping the helper rejection policy narrow for now: explicitly reject iterator/cursor-backed values and avoid inventing broader exclusions without a stronger concrete misuse case.
- The residual G049 protected-value validation item is now considered closed: focused callback coverage includes both the positive shared-cell mutation path and a negative test proving direct captured-root mutation in the worker does not leak across threads.

## Success Criteria

- One active runtime goal captures the remaining hardening work instead of scattering it across completed and exploratory goals.
- The accepted boundary model is explicit: fresh-VM threading, protected shared-value mutation, no general live-graph sharing.
- The project has tests or guards for the most plausible misuse cases that can be caught cheaply.

## Navigation Guide For Agents

- Treat G049 as feature-complete first-slice threading with residual hardening work only.
- Treat G051 as a retired exploration whose main output is "do not use cursor-visited read-only marking as the primary model."
