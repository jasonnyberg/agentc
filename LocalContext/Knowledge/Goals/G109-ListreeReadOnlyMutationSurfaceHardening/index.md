# Goal: G109 — Listree ReadOnly Mutation Surface Hardening

**Status**: COMPLETED
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)  
**Related WorkProduct**: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md)

## Objective
Close mutation paths that can modify recursively frozen/read-only Listree structures, especially removals through `Cursor::remove()` / `ListreeItem::getValue(pop=true)`, so shared worker context and future read-only slab images have a trustworthy logical immutability boundary.

## Rationale
G091 relies on recursively freezing shared worker `context` and `imports`. Research found that assignment into frozen context was refused, but removal could still mutate a frozen dictionary through item-history popping. This weakened worker safety and had to be fixed before scaling async workers or static shared slabs.

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
- [x] Add regression tests proving `/f.a`, `//name`/remove-head style operations, and direct cursor removals cannot mutate recursively frozen trees.
- [x] Audit every `ListreeItem::getValue(pop=true)` and `ListreeItem::addValue(...)` caller.
- [x] Route item-history mutation through parent-aware `ListreeValue`/`Cursor` methods or add an equivalent owner/context guard.
- [x] Ensure `Cursor::remove()` and `Cursor::removeHeadOnly()` refuse mutation under read-only ancestors.
- [x] Ensure cleanup/removal recursion does not delete empty read-only parent structures.
- [x] Preserve existing mutable dictionary history semantics for non-read-only values.
- [x] Update documentation/facts to state the actual hardened ReadOnly guarantee.

## Acceptance Criteria
- [x] A recursively frozen tree cannot be changed by assignment, head-removal, path-removal, list pop, or cleanup recursion.
- [x] G091 worker tests include attempted removal of shared read-only context and prove the coordinator-owned context remains unchanged.
- [x] Direct `ListreeItem` mutation paths are either hidden behind parent guards or explicitly documented as unsafe/internal with tests proving public VM/Cursor paths are safe.
- [x] Existing mutable Listree/Edict dictionary history behavior remains intact outside read-only subtrees.

## Completion Notes — 2026-05-16
Implemented parent-aware item-history mutation guards:

- `Cursor` now tracks `currentParent`, the `ListreeValue` that owns `currentItem`, and refuses `assign`, `remove`, `removeHeadOnly`, and cleanup pops when either the current value or owning parent is read-only.
- `ListreeValue` now exposes parent-aware `addItemValue(...)` and `popItemValue(...)` helpers; public VM/Cursor/tree paths use those helpers instead of mutating `ListreeItem` history directly.
- `ListreeItem::addValue(...)` and `ListreeItem::getValue(pop=true)` remain available as low-level/internal operations, but are explicitly documented as parent-unaware and therefore not the public guarded mutation path.
- Edict freeze regressions now prove `/f.a` and `//f.a` cannot mutate a recursively frozen object.
- G091 intern-worker coverage now attempts both assignment and removal of shared read-only `context.fact` and proves the coordinator-owned context stays unchanged.

Validation:

- `cmake --build build --target reflect_tests edict_tests cpp_agent_tests -j2` — passed.
- `./build/listree/listree_tests --gtest_brief=1` — passed 71/71.
- `./build/tests/reflect_tests --gtest_brief=1` — passed 43/43.
- Focused Edict regression slice `FreezeBuiltin.*:InternWorkerTest.*:EdictVM.*:VMStackTest.*:SimpleAssignTest.*:RegressionMatrixTest.*:PiSimulationTest.MiniKanrenLogicExample` — passed 50/50.
- `./build/cpp-agent/cpp_agent_tests --gtest_brief=1` — passed 49/49.
- Raw smoke `./build/edict/edict -e '{"a":"1"} freeze! @f / f.a /f.a f to_json! print'` now prints `{"a":"1"}` instead of `{}`.

Known outside-slice note: the full unfiltered `edict_tests` target currently reports Cartographer/closure callback failures in legacy FFI tests (`ReproFFITest.AddPoC`, selected `CallbackTest.*`); the maintained focused Edict regression slice above passes and covers the G109 ReadOnly/worker paths.

## Dependencies
High-priority hardening for 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md) and prerequisite confidence for 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md). This slice is now complete for public VM/Cursor/shared-context mutation paths; lower-level mmap/static-slab immutability work remains in G105/G108.
