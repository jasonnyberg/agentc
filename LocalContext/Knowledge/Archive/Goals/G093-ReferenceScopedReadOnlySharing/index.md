# Goal: G093 — Reference-Scoped ReadOnly Sharing

**Status**: COMPLETE
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
- [x] Design note compares value-level ReadOnly vs ref-level ReadOnly vs explicit overlay dictionaries (WP_G093).
- [x] Prototype supports shared frozen structure with local per-worker overrides via `overlay.new!`/`overlay.set!`/`overlay.get!`.
- [x] Tests prove coordinator state is unchanged after worker-local shadow mutation (8 tests + demo).
- [x] Tests cover lookup precedence (shadow > shared) and serialization round-trip via `to_json!`/`from_json!`.
- [x] The design does not weaken existing recursive `LtvFlags::ReadOnly` guarantees (verified in `OverlayDictionaryTest.OverlayDoesNotWeakenReadOnlyGuarantees`).

## Deferral Note
Do not start this before the whole-subtree ReadOnly worker MVP in 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md) proves what granularity is actually needed.


## Completion Summary — 2026-06-21
G093 is complete. The explicit overlay dictionary approach was chosen over ref-level ReadOnly (too invasive — changes slab layout/persistence) and value-level ReadOnly alone (all-or-nothing, no per-key shadowing). 7 new VM opcodes (`VMOP_OVERLAY_*`) implement `overlay.new!`/`set!`/`get!`/`has!`/`keys!`/`shadow_keys!`/`commit!` in `edict_vm_overlay.cpp`. The overlay is a pure data-structure pattern: `{"shared": <frozen>, "shadows": {}}` where the frozen shared base is never mutated and shadow values are ordinary mutable Listree entries. 📄[WP — G093 Reference-Scoped ReadOnly Sharing](../../WorkProducts/WP_G093_ReferenceScopedReadOnlySharing.md). Validation: `OverlayDictionaryTest.*` 8/8, `demo_overlay_dictionary` PASS, full `edict_tests` 198/198.
