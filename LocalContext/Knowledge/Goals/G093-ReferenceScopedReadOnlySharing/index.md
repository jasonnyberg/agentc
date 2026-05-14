# Goal: G093 — Reference-Scoped ReadOnly Sharing

**Status**: DEFERRED  
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)

## Objective
Investigate and implement a finer-grained ReadOnly model where immutability can be attached to `ListreeValueRef` views, enabling shared dictionary structure with worker-local shadow values.

## Source Extracted From
Conversation summary section **“ReadOnly on ListreeValueRef (future refinement)”**.

## Rationale
Whole-subtree ReadOnly is sufficient for the first intern implementation, but future worker sandboxes may need shared object structure with local overrides for selected keys. Reference-scoped ReadOnly could support copy-on-write/shadow-value behavior without mutating coordinator-owned nodes.

## Candidate Capabilities
- Shared dictionary keys/shape remain frozen.
- Worker-local shadow values can override specific bindings.
- A coordinator can inspect worker diffs without exposing mutable shared state.
- Persistence restore continues to strip or normalize transient concurrency flags as appropriate.

## Acceptance Criteria
- [ ] Design note compares value-level ReadOnly vs ref-level ReadOnly vs explicit overlay dictionaries.
- [ ] Prototype supports shared frozen structure with local per-worker overrides.
- [ ] Tests prove coordinator state is unchanged after worker-local shadow mutation.
- [ ] Tests cover lookup precedence and persistence/restore behavior.
- [ ] The design does not weaken existing recursive `LtvFlags::ReadOnly` guarantees.

## Deferral Note
Do not start this before the whole-subtree ReadOnly worker MVP in 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md) proves what granularity is actually needed.
