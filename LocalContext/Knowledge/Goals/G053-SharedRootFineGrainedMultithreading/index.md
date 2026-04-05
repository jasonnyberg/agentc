# G053 - Shared-Root Fine-Grained Multithreading

## Status: PROPOSED

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Builds on the first threading slice from 🔗[`G049-EdictVMMultithreading`](../G049-EdictVMMultithreading/index.md)
- Coexists with the near-term hardening work in 🔗[`G052-RuntimeBoundaryHardening`](../G052-RuntimeBoundaryHardening/index.md)

## Goal

Investigate, design, and eventually implement a concurrency model where multiple threads can share one live VM root and share or mutate Listree items with fine-grained coordination rather than being restricted to fresh-VM workers plus protected shared-value cells.

## Why This Matters

- The current first threading slice is intentionally conservative and useful, but it stops short of true shared in-memory cooperation over one live runtime graph.
- A shared-root model could enable richer long-lived agents, lower-copy coordination, shared world-state updates, and more direct multi-threaded orchestration patterns.
- This is a qualitatively different architectural step from G049/G052: it would turn current "unsupported live-graph sharing" into an explicit, designed runtime capability.

## Core Design Question

How can AgentC permit multiple threads to access one shared VM/Listree root with fine-grained safety and acceptable performance, without corrupting allocator state, traversal state, rewrite/transaction semantics, or cursor/history behavior?

## Investigation Areas

### Ownership and Isolation

- What is the real unit of concurrency control: VM root, ListreeValue, ListreeItem, item-history node, cursor path, or allocator slab object?
- Which operations may run concurrently as read-read, read-write, and write-write?
- How should shared-root semantics interact with rollback, rewrite rules, and nested speculation?

### Data-Structure Semantics

- `ListreeValue` supports tree mode, list mode, item histories, live iterator/cursor values, and mutable insertion/removal.
- `ListreeItem` history stacks allow multiple values per key; any fine-grained concurrency model must specify what happens when threads append, replace, or traverse history simultaneously.
- Cursor navigation and traversal synthesize overlapping live accessors, so a shared-root model must define whether readers hold leases, snapshots, locks, versions, or something else.

### Runtime Substrate

- The slab allocators, container nodes, and blob allocator would need a clearer concurrent ownership/locking model than the current "single-threaded runtime with narrow helper-boundary transfer" assumption.
- The VM resource stack, execution frames, and transaction rollback machinery would need explicit thread-ownership rules if multiple threads can share one root or one VM-adjacent state bundle.

### Candidate Design Families

1. fine-grained locks on mutable Listree/container nodes
2. reader/writer leases or borrows over subtrees/items
3. versioned / MVCC-style snapshots with merge or replace points
4. actor-like root partitioning with explicitly shared sub-objects only
5. hybrid models where mutation funnels through transactional regions with per-object locking

## Constraints

- Do not weaken or confuse G052's current accepted model while this goal is still exploratory.
- Any eventual shared-root design must explain how it coexists with transactions, speculation rollback, FFI callbacks, and cursor traversal.
- The design must be explicit about what remains unsupported even after the first shared-root implementation slice.

## Expected Work Product

- A design investigation note comparing candidate concurrency models for shared-root Listree/VM access.
- A phased implementation plan identifying the smallest viable shared-root slice.
- If the design is accepted, focused runtime/tests/docs changes to land that first slice safely.

## Initial Checklist

- [ ] Inventory current runtime structures that assume single-threaded root ownership.
- [ ] Define the smallest plausible shared-root/fine-grained-sharing slice worth pursuing.
- [ ] Compare at least two candidate concurrency-control models against Listree item history, cursor traversal, and transaction rollback semantics.
- [ ] Decide whether the design target is shared-root across multiple VMs, one truly shared VM, or a staged hybrid.
- [ ] Produce a phased implementation plan before attempting runtime code changes.

## Relationship To G052

- G052 remains the near-term active goal and protects the current conservative threading model.
- G053 is a longer-horizon architecture/design goal for a future, more powerful concurrency model.
- G052 should not expand the supported threading boundary toward G053 prematurely; it should keep the current model narrow until a real design is accepted.

## Navigation Guide For Agents

- Treat this as a deep architecture/design investigation first, not an invitation to opportunistically widen today's runtime semantics.
- Use G052's accepted model as the baseline being intentionally preserved while G053 is explored.
