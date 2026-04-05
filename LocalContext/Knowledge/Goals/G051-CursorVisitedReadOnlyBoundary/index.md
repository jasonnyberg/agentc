# G051 - Cursor-Visited Read-Only Boundary

## Status: RETIRED (REJECTED DIRECTION)

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Related runtime goal: 🔗[`G049-EdictVMMultithreading`](../G049-EdictVMMultithreading/index.md)

## Goal

Evaluate whether marking Listree items or values visited by a `Cursor` as read-only could serve as a concurrency boundary that prevents multiple accessors from modifying the same live Listree region at once.

## Why This Matters

- G049 already chose a conservative first threading model: fresh VM per thread plus explicit protected shared-value cells.
- A cursor-driven read-only boundary sounds attractive because the `Cursor` is the main structured traversal API for Listree graphs.
- If viable, it could provide a stronger runtime-enforced discipline than documentation alone.

## Evidence Snapshot

**Claim**: Cursor-visited read-only marking is not a good primary safety model for G049, though a narrower borrow/guard mechanism could still be useful as a future hardening tool.

**Evidence**:
- Primary: `core/cursor.cpp` shows `Cursor` is a navigation helper, not the sole mutation gateway. Mutations also happen directly through `ListreeValue::find(insert=true)`, `ListreeValue::put(...)`, `ListreeValue::remove(...)`, and `ListreeItem::addValue(...)`.
- Primary: `core/cursor.cpp:393` `Cursor::assign(...)`, `core/cursor.cpp:428` `Cursor::remove(...)`, `core/cursor.cpp:510` `Cursor::create(...)`, and `core/cursor.cpp:517` `Cursor::push(...)` all mutate through underlying Listree APIs that currently have no read-only or ownership check.
- Primary: `listree/listree.h:163-166` and `listree/listree.h:233-256` show mutation lives on both `ListreeItem` and `ListreeValue`, but only `ListreeValue` even has flags today; `ListreeItem` has no flag/lock field for a read-only marker.
- Primary: `core/cursor.cpp:74-75` shows the current cursor lifetime mechanism is just `pin()/unpin()` on the current value, and `listree/listree.h:228-230` shows pinning only tracks a simple count used to avoid removal, not write exclusion.
- Primary: `core/cursor.cpp:53-72` and `core/cursor.cpp:519-540` show cursors are frequently copied/cloned and synthesized during iteration, so any visited-read-only scheme would need refcounted access leases rather than a simple boolean flag.
- Primary: G049's landed first slice already narrows cross-thread mutation to explicit protected shared-value cells; this avoids pretending arbitrary live Listree graphs are safe for concurrent sharing.

**Confidence**: 92%

## Feasibility Assessment

### What would make the idea attractive

- The cursor already names a traversal path and could, in theory, represent a read lease on the region it visits.
- A read-only mark could prevent accidental in-place mutation while another accessor is inspecting the same structure.

### Why it is weak as the primary model

1. **Mutation is not cursor-exclusive**
   - The runtime can mutate Listree through direct `ListreeValue` / `ListreeItem` APIs.
   - A cursor-only rule would be bypassable unless all mutation sites were routed through a new centralized ownership check.

2. **Visited scope is ambiguous**
   - Does visiting one value freeze only that value, its containing item, the whole subtree, sibling history on the item, or the entire root path?
   - The current cursor semantics move across item histories, tail dereferences, and synthesized child cursors, so the protected region is not naturally one object.

3. **A boolean read-only flag is not enough**
   - Multiple cursors can observe the same node simultaneously.
   - Copies, iterators, and traversal callbacks create overlapping accessors.
   - That implies lease/refcount bookkeeping, not a one-bit visited marker.

4. **It conflicts with current Listree usage patterns**
   - `Cursor::assign(...)` appends values to the current item's history stack.
   - `Cursor::forEach(...)` manufactures child cursors over live values.
   - Freezing visited items globally would make ordinary single-threaded traversal surprisingly stateful and fragile.

5. **It still does not solve arbitrary cross-thread sharing cleanly**
   - Even if writes were blocked, live cross-thread sharing would still rely on global allocator/refcount/container correctness.
   - G050 already showed that raw handle lifetime and build-state issues can be subtle even in the narrower helper path.

## Recommended Direction

- Do **not** replace G049's explicit protected shared-value boundary with cursor-visited read-only marking.
- Treat this idea, at most, as a future **debug/hardening** layer.
- If stronger runtime enforcement is desired, prefer one of these narrower models:
  1. protected shared cells remain the only supported mutable cross-thread path
  2. optional borrow-guard metadata on shared-cell snapshots/handles, not on arbitrary Listree traversal
  3. debug-mode assertions that reject mutation of values currently leased by a specific protected-cell access protocol

## Success Criteria For This Investigation

- The project records whether cursor-visited read-only marking should become part of the concurrency model.
- G049 remains grounded in an enforceable first slice rather than a broad implicit “visited means frozen” rule.

## Navigation Guide For Agents

- Read this as an exploratory child of G049, not as a replacement architecture.
- Preserve the distinction between `Cursor` as a navigation API and the deeper mutation/lifetime machinery in Listree, containers, and allocator/refcount paths.

## Retirement Note

- 2026-04-04: Retired into 🔗[`G052-RuntimeBoundaryHardening`](../G052-RuntimeBoundaryHardening/index.md) as a rejected primary direction. Keep it only as rationale for why the project chose explicit protected shared-value boundaries instead.
