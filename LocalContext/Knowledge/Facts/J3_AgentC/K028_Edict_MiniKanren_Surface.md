# Knowledge: Edict Mini-Kanren Surface

**ID**: LOCAL:K028
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #edict, #minikanren, #logic, #examples

## Overview
This entry captures the currently working Mini-Kanren features that can be expressed directly from native Edict source via the native `logic { ... }` block and the older compatibility form `logic_run !`.

## Entry Point
- **Native Form**: `logic { ... }`
- **Compatibility Form**: push a structured query object, then evaluate with `logic_run !`
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

## Working Native Edict Patterns

### 1. Fresh Variable + Equality
```edict
logic {
  "fresh": ["q"],
  "where": [["==", "q", "tea"]],
  "results": ["q"]
}
```

Expected answer set:
```text
[tea]
```

### 2. Multi-Branch Search with `conde`
```edict
logic {
  "fresh": ["q"],
  "conde": [
    [["==", "q", "tea"]],
    [["==", "q", "coffee"]]
  ],
  "results": ["q"]
}
```

Expected answer set:
```text
[tea coffee]
```

### 3. Recursive Membership
```edict
logic {
  "fresh": ["q"],
  "where": [["membero", "q", ["tea", "cake", "jam"]]],
  "results": ["q"]
}
```

Expected answer set:
```text
[tea cake jam]
```

### 4. Contradiction / Empty Result
```edict
logic {
  "fresh": ["q"],
  "where": [["==", "q", "tea"], ["==", "q", "coffee"]],
  "results": ["q"]
}
```

Expected answer set:
```text
[]
```

### 5. Recursive Append
```edict
logic {
  "fresh": ["out"],
  "where": [["appendo", ["tea", "cake"], ["jam"], "out"]],
  "results": ["out"]
}
```

Expected high-level answer:
- one answer representing the appended list `tea cake jam`

### 6. Result Limiting
```edict
logic {
  "fresh": ["q"],
  "where": [["membero", "q", ["tea", "cake", "jam"]]],
  "results": ["q"],
  "limit": "2"
}
```

Expected answer set:
```text
[tea cake]
```

### 7. Multi-Variable Structural Result
```edict
logic {
  "fresh": ["head", "tail"],
  "where": [["conso", "head", "tail", ["tea", "cake"]]],
  "results": ["head", "tail"]
}
```

Expected high-level answer:
- one answer tuple containing `tea` and `cake`

## Semantics Confirmed by Tests
- `edict/tests/logic_surface_test.cpp` confirms:
  - simple `fresh` + `==`
  - multi-result `conde`
  - recursive `membero`
  - contradictory query returning no answers
  - native `logic { ... }` multi-variable output and integrated workflow usage
- `kanren/tests/kanren_tests.cpp` confirms the corresponding runtime relations beneath the Edict surface

## Demonstration Artifact
- `tst/demo_kanren_edict.cpp` is the runnable end-to-end Edict demonstration of the current surface

## Current Constraints
- The native syntax is currently a thin planner block wrapper around the same structured query spec, not a full new infix/planner DSL.
- The older `logic_run !` object-literal bridge remains supported for compatibility.
- Search now uses pull-driven lazy streams with fair branch interleaving for the implemented combinators, but this is still a compact runtime rather than a full canonical miniKanren surface.
- Planner relations operate over the current pair-list/listree encoding used by the runtime bridge.

## Implementation References
- `edict/edict_vm.cpp`
- `edict/edict_types.h`
- `edict/tests/logic_surface_test.cpp`
- `tst/demo_kanren_edict.cpp`
