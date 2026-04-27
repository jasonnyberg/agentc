# Knowledge: Edict Mini-Kanren Surface

**ID**: LOCAL:K028
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #edict, #minikanren, #logic, #examples

## Overview
This entry captures the current Mini-Kanren surface after detachment from compiler/VM ownership. The active model is canonical object/Listree query specs evaluated through imported `kanren` capability paths, with any more ergonomic call-shaped notation implemented as ordinary Edict wrappers.

## Entry Point
- **Canonical Form**: push a structured query object, then evaluate it through an imported evaluator such as `logic!`
- **Wrapper Form**: optional ordinary Edict wrappers can build the same query object before evaluation
- **Result Shape**: pushes a list of answers onto the Edict data stack

## Query Object Fields
- **`fresh`**: list of logic-variable names introduced for the query
- **`where`**: a single clause expressed as a list of logic atoms
- **`conde`**: multiple clauses, each a list of logic atoms
- **`results`**: variables or terms to reify into the output answer set
- **`limit`**: optional numeric string limiting the number of returned answers

## Working Atoms
- **`==`**: unification/equality
- **`membero`**: recursive membership over pair-list encoded Edict arrays
- **`appendo`**: recursive append relation over pair-list encoded Edict arrays

## Working Query Patterns

### 1. Fresh Variable + Equality
```edict
{
  "fresh": ["q"],
  "where": [["==", "q", "tea"]],
  "results": ["q"]
} logic!
```

Expected answer set:
```text
[tea]
```

### 2. Multi-Branch Search with `conde`
```edict
{
  "fresh": ["q"],
  "conde": [
    [["==", "q", "tea"]],
    [["==", "q", "coffee"]]
  ],
  "results": ["q"]
} logic!
```

Expected answer set:
```text
[tea coffee]
```

### 3. Recursive Membership
```edict
{
  "fresh": ["q"],
  "where": [["membero", "q", ["tea", "cake", "jam"]]],
  "results": ["q"]
} logic!
```

Expected answer set:
```text
[tea cake jam]
```

### 4. Contradiction / Empty Result
```edict
{
  "fresh": ["q"],
  "where": [["==", "q", "tea"], ["==", "q", "coffee"]],
  "results": ["q"]
} logic!
```

Expected answer set:
```text
[]
```

### 5. Recursive Append
```edict
{
  "fresh": ["out"],
  "where": [["appendo", ["tea", "cake"], ["jam"], "out"]],
  "results": ["out"]
} logic!
```

Expected high-level answer:
- one answer representing the appended list `tea cake jam`

### 6. Result Limiting
```edict
{
  "fresh": ["q"],
  "where": [["membero", "q", ["tea", "cake", "jam"]]],
  "results": ["q"],
  "limit": "2"
} logic!
```

Expected answer set:
```text
[tea cake]
```

### 7. Multi-Variable Structural Result
```edict
{
  "fresh": ["head", "tail"],
  "where": [["conso", "head", "tail", ["tea", "cake"]]],
  "results": ["head", "tail"]
} logic!
```

Expected high-level answer:
- one answer tuple containing `tea` and `cake`

## Semantics Confirmed by Tests
- `edict/tests/logic_surface_test.cpp` confirms:
  - simple `fresh` + `==`
  - multi-result `conde`
  - recursive `membero`
  - contradictory query returning no answers
  - imported object-spec multi-variable output and integrated workflow usage
- `kanren/tests/kanren_tests.cpp` confirms the corresponding runtime relations beneath the Edict surface

## Demonstration Artifact
- `demo/demo_kanren_edict.cpp` is the runnable end-to-end imported-capability demonstration of the current surface

## REPL vs. VM Commands
- **Special Commands**: `stack`, `help`, `clear`, `exit` are intercepted by the `EdictREPL` loop.
- **Language Words**: Everything else is executed by the VM.
- The canonical runtime contract is still the structured query spec, not a larger dedicated logic sublanguage.
- More ergonomic logic notation should now live in ordinary Edict wrappers or later rewrite-hosted DSL layers rather than compiler special cases.
- Search now uses pull-driven lazy streams with fair branch interleaving for the implemented combinators, but this is still a compact runtime rather than a full canonical miniKanren surface.
- Planner relations operate over the current pair-list/listree encoding used by the runtime bridge.

## Implementation References
- `kanren/runtime_ffi.h`
- `edict/tests/logic_surface_test.cpp`
- `demo/demo_kanren_edict.cpp`
