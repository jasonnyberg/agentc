# WP — G093 Reference-Scoped ReadOnly Sharing: Design Note

## Purpose

G093 investigates finer-grained ReadOnly sharing for intern workers. This design note compares three approaches, documents the chosen implementation, and records the verification evidence.

## The Problem

G091 intern workers share `context`/`imports` through whole-subtree recursive `freeze!` (setting `LtvFlags::ReadOnly` on the entire subtree). This is sufficient for the MVP: the worker cannot mutate any shared binding. But future worker sandboxes may need shared object **structure** with **local overrides** for selected keys — copy-on-write/shadow-value behavior without mutating coordinator-owned nodes.

## Three Approaches Compared

### 1. Value-Level ReadOnly (Current Model)

**Mechanism**: `LtvFlags::ReadOnly` is a one-way flag on `ListreeValue`. Once set via `setReadOnly(recursive=true)`, all descendants are permanently immutable. The flag is stripped during arena persistence (restored nodes start mutable).

**Pros**:
- Already implemented and hardened (G109).
- Zero overhead — a single bit check guards all mutation paths.
- Proven in intern worker tests: assignment, removal, pop, and pruning are all refused.

**Cons**:
- All-or-nothing: a worker either sees the entire shared subtree as frozen or gets its own copy. No per-key shadowing.
- A worker that needs to override one key must deep-copy the entire shared object.

**Verdict**: Correct for the MVP. G091 proved whole-subtree freeze is sufficient. This approach remains the default and is not weakened by the overlay approach.

### 2. Ref-Level ReadOnly (Considered, Rejected for Now)

**Mechanism**: Attach immutability to `ListreeValueRef` views rather than `ListreeValue` nodes. A ref could carry a "shadow" value alongside the frozen shared value, with lookup precedence.

**Pros**:
- Theoretically the most memory-efficient: shared structure stays in place, only overridden refs carry shadow values.
- Could support per-ref copy-on-write without duplicating the parent object.

**Cons**:
- `ListreeValueRef` has no flags field (line 157 of `listree.h`); it's a thin `CPtr<ListreeValue>` wrapper.
- Adding flags to `ListreeValueRef` changes slab layout and the persistence format.
- Every Listree consumer (Cursor, traversal, copy, freeze, persistence) would need to understand ref-level flags.
- The allocator, `ArenaPersistenceTraits`, and `ArenaWatermarkResetTraits` would need updates.
- High implementation risk with no proven worker scenario requiring this granularity yet.

**Verdict**: Too invasive for the current evidence. The slab layout and persistence format are frozen enough that changing `ListreeValueRef` size/semantics is not justified without a concrete need. Revisit if future worker sandboxes prove per-ref shadowing is necessary.

### 3. Explicit Overlay Dictionaries (Chosen)

**Mechanism**: A pure data-structure pattern at the Edict level. An overlay is an ordinary Listree object `{"shared": <frozen>, "shadows": {}}`. The worker mutates only the `shadows` dict. A lookup helper checks shadows first, then falls through to the frozen shared base. The frozen shared base is never touched.

**Pros**:
- No changes to Listree internals, slab layout, or persistence format.
- Works with the existing `LtvFlags::ReadOnly` guarantee — the shared base stays frozen.
- `shadows` is an ordinary mutable Listree object; it serializes with `to_json!`/`from_json!`.
- `overlay.commit!` extracts the shadow diff for coordinator inspection without exposing mutable shared state.
- The overlay is a first-class Edict value — it can be passed through `speculate`, transactions, and `intern_run!` task envelopes.
- 7 VM opcodes (`VMOP_OVERLAY_*`) backed by a single C++ translation unit.

**Cons**:
- The overlay is a separate object, not a transparent wrapper over the shared base. Workers must use `overlay.get!` rather than dot-notation field access.
- Shadow values are copies, not references — changes to the shared base after overlay creation are not visible through existing shadow entries (but are visible for unshadowed keys via fall-through).

**Verdict**: Achievable with current information, low risk, and satisfies all G093 acceptance criteria. This is the implemented approach.

---

## Implementation

### VM Opcodes

7 new opcodes in `edict_types.h`, implemented in `edict_vm_overlay.cpp`:

| Opcode | Edict Word | Stack | Behavior |
|--------|-----------|-------|----------|
| `VMOP_OVERLAY_NEW` | `overlay.new!` | `( shared -- overlay )` | Create `{"shared": shared, "shadows": {}}` |
| `VMOP_OVERLAY_SET` | `overlay.set!` | `( overlay key value -- overlay )` | Set shadow value for key |
| `VMOP_OVERLAY_GET` | `overlay.get!` | `( overlay key -- value )` | Shadow lookup → shared fallthrough → null |
| `VMOP_OVERLAY_HAS` | `overlay.has!` | `( overlay key -- ok )` | `["ok"]` if key in shadow or shared |
| `VMOP_OVERLAY_KEYS` | `overlay.keys!` | `( overlay -- list )` | Merged key list (shadow + shared-only) |
| `VMOP_OVERLAY_SHADOW_KEYS` | `overlay.shadow_keys!` | `( overlay -- list )` | Only shadowed keys (the worker-local diff) |
| `VMOP_OVERLAY_COMMIT` | `overlay.commit!` | `( overlay -- shadows )` | Extract shadow dict for coordinator inspection |

Registered as the `overlay` bootstrap capsule in `edict_vm_bootstrap.cpp`.

### Edict Module

`cpp-agent/edict/modules/overlay.edict` — a thin Edict wrapper re-exporting the bootstrap capsule words under `overlay.*`.

### Acceptance-Criteria Application

`demo/demo_overlay_dictionary.cpp` — a simulated coordinator/worker scenario:

1. Coordinator creates `{"model": "gpt-4", "temperature": "0.7", "max_tokens": "2048", "system_prompt": "..."}` and `freeze!`s it.
2. Worker creates an overlay via `overlay.new!`.
3. Worker shadows `temperature` → `0.2`, `max_tokens` → `512`, adds `custom_instruction`.
4. Worker reads effective values: shadowed keys return shadow values, unshadowed keys fall through to shared.
5. Coordinator inspects `overlay.commit!` and `overlay.shadow_keys!` to see the worker's diff.
6. Coordinator reads `shared_config.temperature` — still `0.7`, unchanged.
7. Asserts: `ReadOnly preserved: YES`, `Coordinator unchanged: YES`, `Worker saw shadow value: YES`.

Output: `PASS: Overlay dictionary provides reference-scoped ReadOnly sharing.`

### Verification Matrix

| Test | What it proves |
|------|---------------|
| `OverlayDictionaryTest.OverlayGetReturnsShadowValueOverSharedValue` | Shadow precedence in lookup |
| `OverlayDictionaryTest.OverlayGetFallsThroughToSharedForUnshadowedKey` | Shared fallthrough for non-shadowed keys |
| `OverlayDictionaryTest.CoordinatorStateUnchangedAfterShadowMutation` | Coordinator frozen state unchanged after worker shadow mutation |
| `OverlayDictionaryTest.OverlayHasChecksBothShadowAndShared` | `has!` checks both layers |
| `OverlayDictionaryTest.OverlayKeysReturnsMergedKeyList` | `keys!` merges shadow + shared; `shadow_keys!` returns only diff |
| `OverlayDictionaryTest.OverlayCommitExtractsShadowsForCoordinatorInspection` | `commit!` extracts shadow dict without shared noise |
| `OverlayDictionaryTest.OverlaySurvivesSerializationRoundTrip` | Shadows serialize via `to_json!`/`from_json!` |
| `OverlayDictionaryTest.OverlayDoesNotWeakenReadOnlyGuarantees` | Shared stays ReadOnly; shadows stay mutable |
| `demo_overlay_dictionary` | End-to-end coordinator/worker scenario with isolation proof |

### Persistence Behavior

The overlay is an ordinary Listree object. The `shadows` dict serializes normally via `to_json!`. The `shared` field points at a frozen `ListreeValue` that carries `LtvFlags::ReadOnly`; during arena persistence, the ReadOnly flag is stripped and the node is restored mutable — the caller re-freezes if needed (existing G096 behavior). The overlay structure itself has no special persistence requirements.

---

## What This Does Not Weaken

- `LtvFlags::ReadOnly` remains a one-way flag. The overlay never clears it.
- `freeze!` continues to work exactly as before.
- G109 ReadOnly mutation-surface hardening is unaffected — the overlay's `shadows` dict is an independent mutable object.
- G091 intern worker safety rules are unaffected — workers can use overlays alongside the existing `context`/`imports` freeze mechanism.

## Future Work

- Integrate overlays into `intern_run!` task envelopes as a first-class worker-scoping mechanism.
- Add `overlay.delete!` for removing a shadow entry (restoring pure fallthrough to shared).
- Investigate ref-level ReadOnly if a concrete worker scenario proves per-key shadowing on the same object (without a separate overlay) is necessary.
