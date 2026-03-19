# Knowledge: J3 Term Rewrite Runtime

**ID**: LOCAL:K027
**Category**: [J3 AgentC](index.md)
**Tags**: #j3, #rewriting, #optimizer, #vm

## Overview
J3 now has an active runtime term-rewrite path in the Edict VM. Rewrite rules operate on the active data-stack suffix and can replace matched sequences before normal execution continues.

## Implemented Semantics
- **Activation Point**: Rewrite processing runs from the VM epilogue in `edict/edict_vm.cpp` after ordinary opcode execution, unless the VM is in error, yield, or scan mode.
- **Match Shape**: Rules match stack suffixes.
- **Precedence**: Longest matching suffix wins.
- **Tie-Break**: Equal-length matches use earliest-defined rule order.
- **Wildcards**: Tokens like `$1` capture matched stack values and can be substituted into replacements.
- **Safety Bound**: `applyRewriteLoop()` uses a bounded step count to prevent infinite self-rewrite loops.

## Runtime Surface
- Rules are currently registered through `EdictVM::addRewriteRule(...)`.
- The language-level rewrite syntax goal (`G015`) is still pending.

## Verification
- Focused runtime coverage exists in `edict/tests/rewrite_runtime_test.cpp`.
- A runnable demonstration exists in `tst/demo_rewrite_runtime.cpp`.
- Full `edict_tests` remained green after rewrite activation (`35` passing on 2026-03-07).

## Known Limits
- Rewriting is currently runtime-only; compiler-as-rewriter work is still separate future work.
- Wildcards currently capture single stack values only; no variadic capture is implemented.
