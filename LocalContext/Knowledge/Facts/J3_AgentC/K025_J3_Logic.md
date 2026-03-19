# Knowledge: J3 Mini-Kanren (Logic)

**ID**: LOCAL:K025
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #logic, #minikanren, #unification, #reasoning

## Overview
J3 integrates a Mini-Kanren relational programming engine to enable logic planning, type inference, and goal-directed reasoning directly within the AgentC environment.

## Components

### `State`
- **Immutable**: Represents a snapshot of logical variable substitutions.
- **Substitution**: Stored as a `ListreeValue` (AATree), mapping logic variables to values (or other variables).
- **Methods**:
  - `walk(v)`: Recursively resolves a variable against the substitution.
  - `unify(u, v)`: Attempts to unify two terms, returning a new `State` (or null on failure).
  - `extend(var, val)`: Binds a variable.

### `Goal`
- **Definition**: A function `State -> Stream<State>`.
- **Stream**: An eager `std::vector` of `shared_ptr<State>` representing deterministic search results.
- **Primitives**:
  - `equal(u, v)`: Unifies `u` and `v`.
  - `call_fresh(f)`: Introduces a new logic variable.
  - `disj(lhs, rhs)`: Appends results from alternative branches.
  - `conj(lhs, rhs)`: Threads substitutions from one goal into the next.
  - `conde(...)`: Multi-branch logic built from conjunctions of goals.
  - `disj_fair(lhs, rhs)`: round-robins finite branch result streams.
  - `conde_fair(...)`: round-robins finite clause result streams.
  - `run(...)`: Reifies query variables into stable result values.

## Integration
- **Logic Variables**: Represented by `ListreeValue` with specific flags (likely `LtvFlags::LogicVar`), storing a unique ID.
- **Reification**: Query outputs are normalized into stable human/test-readable forms like `_0`, `_1` for unbound logic variables.
- **Slab Synergy**: `G011` now provides allocator/VM checkpoint support for rollback-capable cognitive scratch execution; the Mini-Kanren runtime itself still uses eager state copying rather than allocator-backed backtracking.

## Status
- **Implemented Runtime Core**: Includes branching combinators, reification, and `run(...)`, with recursive bounded relations validated via tests.

## 2026-03-07 Update
- `kanren/kanren.cpp` now supports `disj`, `conj`, `conj_all`, `conde`, `reify`, and `run`.
- Unification now descends through Listree list/tree structure well enough to support bounded recursive examples.
- Recursive relations such as `membero` and `appendo` are verified in `kanren/tests/kanren_tests.cpp`.
- A runnable reasoning demo exists in `tst/demo_kanren_reasoning.cpp`.
- Added first-pass fair finite-branch interleaving helpers (`disj_fair`, `conde_fair`) plus focused coverage for their ordering behavior.
- Edict now exposes a thin logic surface through `logic_run !`, with structured query objects carrying `fresh`, `where`/`conde`, and `results` fields.
- Language-level logic coverage exists in `edict/tests/logic_surface_test.cpp`, with a runnable Edict demo in `tst/demo_kanren_edict.cpp`.

## Known Limits
- Search is still eager overall. `disj_fair` and `conde_fair` improve result interleaving for finite branch streams, but recursive infinite branches still need a later lazy-stream redesign.
- The current Edict surface is intentionally narrow and data-driven; broader planner syntax is still future work.
