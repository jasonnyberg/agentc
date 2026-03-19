# Cognitive Core Source Examples

This document shows the currently working Edict-facing cognitive-core source forms.

## Logic Query
```edict
logic {
  "fresh": ["q"],
  "conde": [
    [["==", "q", "tea"]],
    [["membero", "q", ["cake", "jam"]]]
  ],
  "results": ["q"]
}
```

This returns an answer list containing `tea`, `cake`, and `jam`.

The older compatibility form still works:
```edict
{"fresh": ["q"],
 "conde": [
   [["==", "q", "tea"]],
   [["membero", "q", ["cake", "jam"]]]
 ],
 "results": ["q"]}
logic_run !
```

## Source-Defined Rewrite Rule
```edict
{"pattern": ["dup", "dot", "sqrt"],
 "replacement": ["magnitude"]}
rewrite_define ! /
"dup" "dot" "sqrt"
```

This rewrites the stack suffix into `magnitude`.

## Rewrite Introspection And Removal
```edict
{"pattern": ["alpha"], "replacement": ["first"]}
rewrite_define ! /
{"pattern": ["beta"], "replacement": ["second"]}
rewrite_define ! /
rewrite_list ! /
"0" rewrite_remove ! /
rewrite_list !
```

This lists the active rules, removes rule `0`, then lists the remaining rules again.

## Logic + Rewrite Together
```edict
{"pattern": ["dup", "dot", "sqrt"],
 "replacement": ["magnitude"]}
rewrite_define ! /

logic {
  "fresh": ["q"],
  "where": [["membero", "q", ["tea", "cake"]]],
  "results": ["q"],
  "limit": "2"
} @answers /

answers /

"dup" "dot" "sqrt"
```

This sequence demonstrates that the current Edict surface can:
- define a rewrite rule from source
- run a logic query from source through the native `logic { ... }` block
- feed the resulting answer list into subsequent Edict steps through `@answers`
- continue executing ordinary rewritten stack programs

For rollback-backed integrated validation, run `tst/demo_cognitive_core_validation.cpp` through the build output.
