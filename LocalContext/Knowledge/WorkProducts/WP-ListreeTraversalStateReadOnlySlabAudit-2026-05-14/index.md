# WP: Listree Traversal State and ReadOnly Slab Audit (2026-05-14)

## Summary

This work product records a research-only audit of Listree traversal state, slab-resident mutable metadata, and the implications for parallel read-only traversal and the layered mmap micro-VM architecture. No code was modified as part of this audit.

The headline findings:

1. Current traversal visited state is already external to slab-resident `ListreeValue` flags: `TraversalContext` stores `absolute_visited` and `recursive_visited` as `std::unordered_set<SlabId>`.
2. Replacing those sets with cursor/traversal-scoped per-slab bitmaps is still a strong improvement for efficiency, determinism, and future layer-aware traversal.
3. The larger blockers for read-only mmap slabs are other slab-resident mutations during normal read/navigation/execution: allocator `inUse` refcounts, `ListreeValue::pinnedCount`, code-frame `.ip` mutation, and incomplete `ReadOnly` enforcement around `ListreeItem` history popping.
4. A concrete `ReadOnly` gap was observed: a recursively frozen dictionary could be mutated by path removal through `Cursor::remove()` / `ListreeItem::getValue(pop=true)`. This public VM/Cursor gap was fixed by 🔗[G109 — Listree ReadOnly Mutation Surface Hardening](../../Goals/G109-ListreeReadOnlyMutationSurfaceHardening/index.md) on 2026-05-16.

## Evidence Snapshot

### Traversal state is already external

Primary code evidence:

- `listree/listree.h` defines `TraversalContext` with `absolute_visited` and `recursive_visited` sets.
- `listree/listree.cpp::ListreeValue::traverse(...)` uses `context->is_absolute(...)`, `context->mark_absolute(...)`, `context->is_recursive(...)`, `context->mark_recursive(...)`, and `context->unmark_recursive(...)`.
- Current `LtvFlags` contains no `Absolute`, `Recursive`, or `Visited` flags.

Current `LtvFlags` are storage/semantics/policy bits such as `Duplicate`, `Own`, `Binary`, `Null`, `Immediate`, `StaticView`, `SlabBlob`, `LogicVar`, `Iterator`, `List`, and `ReadOnly`.

### Concrete ReadOnly removal gap — fixed in G109

A direct raw-Edict smoke showed that a frozen tree could be structurally changed by removal before 🔗[G109 — Listree ReadOnly Mutation Surface Hardening](../../Goals/G109-ListreeReadOnlyMutationSurfaceHardening/index.md):

```bash
./build/edict/edict -e '{"a":"1"} freeze! @f / f.a /f.a f to_json! print'
```

Observed output included:

```json
{}
```

with the removed value left on the stack. That means `/f.a` removed `a` from `f` despite `f` being recursively frozen.

Likely cause from code inspection:

- `ListreeValue::remove(...)`, `ListreeValue::find(insert=true)`, `ListreeValue::put(...)`, and `ListreeValue::get(pop=true)` have read-only checks.
- `Cursor::remove()` can bypass the parent `ListreeValue::remove(...)` guard by calling `currentItem->getValue(true, currentItemFromEnd)`.
- `ListreeItem::getValue(pop=true)` has no owner/backpointer and no read-only check, so it can pop item-history values even when the owning parent tree is read-only.

This gap mattered for worker safety: earlier intern tests proved assignment into read-only context was refused, but did not prove removal was refused. G109 now adds parent-aware item-history mutation helpers plus VM/Cursor and G091 worker regressions proving assignment, path removal, remove-head, list pop, and cleanup recursion cannot mutate public recursively frozen/shared-context paths.

## Bitmap Traversal Context Opportunity

Current `TraversalContext` uses two `std::unordered_set<SlabId>` sets. A more slab-native traversal context can use per-slab bitmaps owned by the traversal scope rather than by slab-resident nodes.

Given current constants:

```text
SLAB_SIZE = 1024
NUM_SLABS = 4096
```

One bitmap per slab needs:

```text
1024 bits = 128 bytes
```

Two bitmaps for traversal state need:

```text
seen/global visited       128 bytes
active/current recursion  128 bytes
-----------------------------------
per visited slab          256 bytes
```

Recommended naming:

- `seen` instead of `absolute_visited` — nodes already visited globally for this traversal.
- `active` instead of `recursive_visited` — nodes currently on the DFS recursion stack.

Potential shape:

```cpp
struct SlabVisitBits {
    std::array<uint64_t, SLAB_SIZE / 64> seen;
    std::array<uint64_t, SLAB_SIZE / 64> active;
};

struct VisitSlabKey {
    uint16_t layer_id;   // future layered mmap support
    uint16_t slab_index;
};

class TraversalVisitState {
public:
    bool seen(SlabId id) const;
    void markSeen(SlabId id);
    bool active(SlabId id) const;
    void markActive(SlabId id);
    void clearActive(SlabId id);
};
```

For current non-layered code, `layer_id` can be implicit. For full-send layered mmap slabs, traversal identity should eventually include layer/arena identity so different mounted slabs with the same local slab index cannot collide.

## Cursor-Owned vs Operation-Owned State

Traversal state should be **operation-scoped**, not permanent cursor state. A cursor may create/own a traversal state while prosecuting one traversal, but repeated traversals over the same cursor should not accidentally inherit old visited bits.

Preferred model:

```cpp
TraversalVisitState state;
cursor.traverse(callback, options, state);
```

or a traversal session object:

```cpp
cursor.beginTraversal().walk(callback);
```

This keeps transient traversal metadata out of slab-resident values and avoids persistent cursor-level state leaks.

## Slab-Resident Mutable State Inventory

The following current mutation sources matter for parallel read-only traversal, static core images, or process-isolated micro-VMs.

| Mutation source | Location | Why it matters |
|-----------------|----------|----------------|
| Traversal visited state | `TraversalContext` sets, not slab nodes | Already external; bitmap context can improve it and make it layer-aware. |
| `CPtr` retain/release | allocator `inUse` metadata | Mutates slab metadata even during reads/traversal; incompatible with OS read-only mapped slabs unless static slabs are immortal or sidecar-refcounted. |
| Cursor pinning | `ListreeValue::pinnedCount` | Cursor navigation/copy/destruction writes node-local atomics; incompatible with read-only mapped static slabs. |
| `ReadOnly` flag | `ListreeValue::flags` | Runtime freeze mutates nodes; fine for private dynamic slabs, but static images should be immutable by layer/manifest/OS mapping. |
| `List` / `Null` flags | e.g. `op_SPLICE` uses `setFlags(List)` and `clearFlags(Null)` | Value kind can mutate during normal Edict operation; static/shared values must reject or copy-on-write this. |
| code-frame `.ip` | `EdictVM::writeFrameIp(...)` writes `.ip` under binary frame nodes | Static bytecode sharing requires immutable code objects split from private activation frames. |
| `ListreeItem` value history | `ListreeItem::addValue(...)`, `getValue(pop=true)` | Can mutate tree history independent of parent `ReadOnly`; current safety gap. |
| CLL/AATree links | `store`, `splice`, `remove`, `add` | Normal structural mutation; must remain private/owner-only for shared slabs. |
| `Iterator` payload | `LtvFlags::Iterator` stores live `Cursor*` | Process/thread-local runtime object; must not enter static shared slabs. |
| storage/destructor flags | `Duplicate`, `Own`, `Free`, `StaticView`, `SlabBlob`, `Immediate` | Some imply process-local pointer/free behavior; static images should normalize storage and avoid process-local ownership. |
| rollback/reset | `resetTransientListreeValue(...)` mutates flags/list/tree/payload | Allocator reclamation must not touch static slabs. |

## Flag Assessment

### Generally intrinsic / static-descriptive

These can be slab-resident as descriptive metadata, but should be immutable for static layers:

- `Binary`
- `Null`
- `Immediate`
- `SlabBlob`
- `LogicVar`
- `List`

Caveat: `List` and `Null` are currently mutated in some runtime paths, so static values must reject those operations or copy into private dynamic slabs first.

### Process-local / unsafe for static slabs

- `Iterator` — contains a live `Cursor*`; persistence already rejects this kind of value.
- `StaticView` — safe only if the view points into a known mapped image with stable lifetime/addressing semantics.
- `Own` / `Duplicate` / `Free` — imply destructor/free behavior; static images should not rely on raw process-local ownership.

### Runtime policy/state

- `ReadOnly` — useful for dynamic subtree freezing, but static core image immutability should be expressed by layer/manifest/OS mapping, not by setting a per-node bit at runtime.

## Relationship to Full-Send Slab Architecture

The audit refines the full-send slab plan:

- G091 async workers can still use JSON/event handoff first, but shared context safety needs stronger `ReadOnly` enforcement.
- G104 must split immutable code objects from mutable activation frames because `.ip` is currently stored under binary code frames.
- G105 must define static slab ownership, including no-write retain/release and cursor pin semantics for read-only slabs.
- G108 should replace traversal set state with per-slab bitmaps and make traversal state layer-aware.
- G109 should close the concrete `ReadOnly` mutation surface gap before shared read-only context is trusted for untrusted/parallel worker code.

## Recommended Staged Goals

1. **G109 — Listree ReadOnly Mutation Surface Hardening**: fix/remove bypasses around `Cursor::remove()`, `Cursor::removeHeadOnly()`, and direct `ListreeItem` history mutation.
2. **G108 — Cursor-Scoped Traversal Visit Bitmaps**: replace set-based traversal context with operation-scoped slab bitmaps, designed for future layer-aware traversal.
3. **G105 — ReadOnly Static Slab Ownership Model**: make static slab retain/release and cursor pinning safe without writes to read-only mappings.
4. **G104 — Immutable Code Object / Activation Frame Split**: move `.ip` and activation-local metadata out of static-shareable bytecode objects.

## Open Questions

- Should traversal visit bitmaps track only `ListreeValue` nodes, or should a generic traversal state also track `ListreeItem`, `ListreeValueRef`, `CLL`, and `AATree` allocator families?
- Should static-layer `pin()`/`unpin()` be no-op, sidecar-tracked, or prohibited?
- Should `ListreeItem` gain owner/context awareness, or should all item-history mutation be hidden behind parent `ListreeValue` methods?
- Should `ReadOnly` become a layer property for static slabs while remaining a per-node dynamic flag for private slabs?
- Should static slab traversal APIs use borrowed references to avoid `CPtr` refcount writes entirely?

## Conclusion

Traversal visited bits are not currently stored in slab-resident `LtvFlags`, so the original concern has already been partly addressed. However, a bitmap traversal state is still worthwhile. The more urgent correctness issue is that normal read/navigation/execution paths still mutate slab-resident operational state, and the observed `ReadOnly` removal bypass weakens worker shared-context safety.

For full-send micro-VMs, all transient traversal-local, cursor-local, activation-local, and process-local state must move out of shared slab-resident objects and into traversal scope, VM-private overlays, process-local sidecars, or Root1/image manifests.
