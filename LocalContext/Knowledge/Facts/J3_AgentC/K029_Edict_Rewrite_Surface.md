# Knowledge: Edict Rewrite Surface

**ID**: LOCAL:K029
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #edict, #rewriting, #optimizer, #examples

## Overview
Edict can now define runtime rewrite rules directly from source using a structured rule object plus `rewrite_define !`, inspect the active rule set with `rewrite_list !`, remove a rule by index with `rewrite_remove !`, control rewrite execution mode with `rewrite_mode !`, trigger rewrites manually with `rewrite_apply !`, and inspect the last rewrite decision with `rewrite_trace !` or `EdictVM::getLastRewriteTrace()`.

## Canonical Source Form
```edict
{"pattern": ["dup", "dot", "sqrt"],
 "replacement": ["magnitude"]}
rewrite_define !
```

## Required Fields
- **`pattern`**: list of string tokens to match against the active stack suffix
- **`replacement`**: list of string tokens to push when the rule matches

## Runtime Behavior
- Rules are registered into the same VM rewrite store used by `EdictVM::addRewriteRule(...)`.
- Once registered, runtime rewrite execution remains governed by the active `G014` semantics:
  - longest suffix wins
  - equal-length ties keep earliest registration order
  - `$1`, `$2`, ... wildcard captures can be substituted into replacements
  - `#list` matches a list-valued stack item
  - `#atom` matches a non-list stack item
  - bounded loop protection remains active
- Rewrite execution mode is now caller-controlled:
  - `auto` keeps the original epilogue-driven behavior
  - `manual` disables automatic rewriting until `rewrite_apply !`
  - `off` disables rewrite execution entirely
- The VM records the last rewrite decision as a structured trace with fields such as `status`, `reason`, `mode`, and, when relevant, `index`, `pattern`, and `replacement`.

## Working Native Edict Examples

### 1. Optimization-Shaped Rule
```edict
{"pattern": ["dup", "dot", "sqrt"],
 "replacement": ["magnitude"]}
rewrite_define ! /
"dup" "dot" "sqrt"
```

Expected stack result:
```text
[magnitude]
```

### 2. Wildcard Substitution Rule
```edict
{"pattern": ["$1", "x"],
 "replacement": ["x", "$1"]}
rewrite_define ! /
"tea" "x"
```

Expected stack result:
```text
[tea x]
```

### 3. Malformed Rule Rejection
```edict
{"pattern": ["x"],
 "replacement": [{}]}
rewrite_define !
```

Expected runtime error:
```text
replacement entries must be strings
```

### 4. Rule Listing And Removal
```edict
{"pattern": ["alpha"], "replacement": ["first"]}
rewrite_define ! /
{"pattern": ["beta"], "replacement": ["second"]}
rewrite_define ! /
rewrite_list ! /
"0" rewrite_remove ! /
rewrite_list !
```

Expected behavior:
- the first `rewrite_list !` returns both registered rules with `index`, `pattern`, and `replacement`
- `"0" rewrite_remove !` returns the removed rule object
- the second `rewrite_list !` returns only the `beta -> second` rule

### 5. Manual Rewrite Scope Control
```edict
{"pattern": ["x"], "replacement": ["manual-hit"]}
rewrite_define ! /
"manual" rewrite_mode ! /
"x"
rewrite_apply !
```

Expected behavior:
- the initial `"x"` remains untouched while mode is `manual`
- `rewrite_apply !` applies the rule once and returns a trace object describing the match

### 6. Type-Aware Pattern Token
```edict
{"pattern": ["#atom", "x"], "replacement": ["atom-hit"]}
rewrite_define ! /
"tea" "x"
```

Expected stack result:
```text
[atom-hit]
```

## Verification
- `edict/tests/rewrite_language_test.cpp` covers:
  - successful source-defined rule registration
  - malformed rule rejection
  - registration order preservation
  - end-to-end source-to-execution rewrite behavior
  - rule listing and indexed removal
  - manual/off rewrite scope control
  - type-aware pattern matching and trace inspection
- `tst/demo_rewrite_language.cpp` is the runnable demonstration of the current surface

## Current Constraints
- The current language surface is still object-driven; there is not yet a more concise `rewrite:` sugar form.
- Richer matching currently means type-aware stack-item classes plus the earlier `$n` captures, not arbitrary deep structural rewrite patterns.
- Rule removal currently uses a numeric string index and compacts later indices.
- Rewrite scoping is VM-phase oriented (`auto`/`manual`/`off`), not yet partitioned by named rule sets or lexical regions.

## Implementation References
- `edict/edict_vm.cpp`
- `edict/edict_types.h`
- `edict/tests/rewrite_language_test.cpp`
- `tst/demo_rewrite_language.cpp`
