# Session 2026-04-19

## Summary

Investigated the feasibility of adding a read-only marker to Listree branches for cross-VM sharing, determined it is sound and implementable, and created G058 with a full six-slice implementation plan.

## Work Completed

- Deep-read `listree/listree.h`, `listree/listree.cpp`, `core/alloc.h`, `core/container.h`, `edict/edict_vm.h`, `edict/edict_vm.cpp` to understand the full mutation surface, allocator model, transaction/rollback semantics, and thread-spawn patterns.
- Read the existing G053 goal to confirm this is a distinct, narrower problem (read-only sharing vs. mutable sharing).
- Assessed five key correctness properties:
  1. Ref counting is already thread-safe (Allocator mutex).
  2. Concurrent reads of frozen node content are safe under C++11 once no writer runs.
  3. Transaction rollback watermarks cannot reclaim pre-watermark read-only nodes.
  4. Mutation paths are 4 localized methods — guards are cheap.
  5. `copy()` on a read-only node can short-circuit to O(1) CPtr retain.
- Identified the one real hazard: `pinnedCount` is a plain `int` that would race under concurrent cursor traversal. Fix: `std::atomic<int>`.
- Created G058 goal file with six implementation slices (A: core flag/guards; B: copy shortcut; C: VM freeze points; D: spawn optimization; E: tests; F: docs) and estimated scope (~270 lines total).
- Updated Dashboard: added G058 to active goals, updated recommended next steps and Last Updated date.

## Key Findings

- **Claim**: A `LtvFlags::ReadOnly` bit + 4 mutation guards + `std::atomic<int> pinnedCount` is sufficient for safe cross-VM read-only sharing. **Confidence: High (90%+).** Evidence: direct code analysis of all mutation paths, allocator semantics, and transaction rollback logic.
- **Main benefit**: Thread spawn / closure thunk currently calls `root->copy()` (O(N) deep copy of the entire library tree). With read-only branches, this becomes O(1) CPtr retain.
- G058 does not widen the accepted mutable threading model; G052's conservative baseline is fully preserved.

## Links

- New goal: 🔗[`G058-ReadOnlyListreeBranches`](../../../Goals/G058-ReadOnlyListreeBranches/index.md)
- Broader design context: 🔗[`G053-SharedRootFineGrainedMultithreading`](../../../Goals/G053-SharedRootFineGrainedMultithreading/index.md)
- Dashboard: 🔗[`LocalContext/Dashboard.md`](../../../../../Dashboard.md)
