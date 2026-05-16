# Goal: G109 — Listree ReadOnly Mutation Surface Hardening

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)  
**Related WorkProduct**: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md)

## Objective
Close mutation paths that can modify recursively frozen/read-only Listree structures, especially removals through `Cursor::remove()` / `ListreeItem::getValue(pop=true)`, so shared worker context and future read-only slab images have a trustworthy logical immutability boundary.

## Rationale
G091 currently relies on recursively freezing shared worker `context` and `imports`. Research found that assignment into frozen context is refused, but removal can still mutate a frozen dictionary through item-history popping. This weakens worker safety and must be fixed before scaling async workers or static shared slabs.

Observed smoke:

```bash
./build/edict/edict -e '{"a":"1"} freeze! @f / f.a /f.a f to_json! print'
```

Observed result included `{}`, meaning `/f.a` removed `a` from a frozen object.

## Suspected Mutation Gap
- `ListreeValue::find(insert=true)`, `put`, `remove`, and `get(pop=true)` have read-only guards.
- `Cursor::remove()` calls `currentItem->getValue(true, currentItemFromEnd)` directly.
- `ListreeItem::getValue(pop=true)` has no owner/backpointer and no read-only check.
- `ListreeItem::addValue(...)` similarly mutates item history without knowing the parent value's read-only status.

## Implementation Plan
- [ ] Add regression tests proving `/f.a`, `//@name`/remove-head style operations, and direct cursor removals cannot mutate recursively frozen trees.
- [ ] Audit every `ListreeItem::getValue(pop=true)` and `ListreeItem::addValue(...)` caller.
- [ ] Route item-history mutation through parent-aware `ListreeValue`/`Cursor` methods or add an equivalent owner/context guard.
- [ ] Ensure `Cursor::remove()` and `Cursor::removeHeadOnly()` refuse mutation under read-only ancestors.
- [ ] Ensure cleanup/removal recursion does not delete empty read-only parent structures.
- [ ] Preserve existing mutable dictionary history semantics for non-read-only values.
- [ ] Update documentation/facts to state the actual hardened ReadOnly guarantee.

## Acceptance Criteria
- [ ] A recursively frozen tree cannot be changed by assignment, head-removal, path-removal, list pop, or cleanup recursion.
- [ ] G091 worker tests include attempted removal of shared read-only context and prove the coordinator-owned context remains unchanged.
- [ ] Direct `ListreeItem` mutation paths are either hidden behind parent guards or explicitly documented as unsafe/internal with tests proving public VM/Cursor paths are safe.
- [ ] Existing mutable Listree/Edict dictionary history behavior remains intact outside read-only subtrees.

## Dependencies
High-priority hardening for 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md) and prerequisite confidence for 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md). It should be completed before trusting async workers with shared read-only context.
