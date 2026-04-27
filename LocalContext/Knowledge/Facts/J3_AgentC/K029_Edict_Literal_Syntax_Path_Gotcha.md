# Knowledge: Edict Literal Syntax — Path Quoting Gotcha

**ID**: LOCAL:K029
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #edict, #ffi, #syntax, #gotcha

## Summary
When passing file paths to Edict's `[...]` literal syntax, do NOT include inner quotes.
The `[...]` delimiter captures its content verbatim — including any single or double quotes.

## The Gotcha
```edict
['/path/to/file.so']    ← WRONG: path starts with ' — file not found
["/path/to/file.so"]    ← WRONG: path starts with " — file not found
[/path/to/file.so]      ← CORRECT: bare path, no surrounding quotes
```

## Why It Matters
The `resolver.import !` and `parser.__native.map !` operations receive the literal string
value off the stack. If the path contains a leading quote character, libclang / the filesystem
will fail to find the file, producing a cryptic "Failed to map header" error.

## Verified Working Pattern
```edict
[/home/user/project/build/kanren/libkanren.so] [/home/user/project/cartographer/tests/kanren_runtime_ffi_poc.h] resolver.import ! @logicffi
logicffi.agentc_logic_eval_ltv @logic
{ "fresh": ["q"], "where": [["==", "q", "tea"]], "results": ["q"] } logic !
print
```

## Discovery Context
This was discovered while reproducing the Mini-Kanren logic socket integration demo
in session 2026-04-27. All previous socket attempts had used `['path']` syntax.
