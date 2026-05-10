# Goal: G082 — Edict Prefix Sigil Chain Semantics

**Status**: COMPLETE  
**Created**: 2026-05-10  
**Completed**: 2026-05-10

## Objective
Align Edict's no-space prefix-sigil parsing with the intended language semantics: concatenated sigils before a label apply to that same label in left-to-right order.

## Rationale
The LLM guide exposed a critical mismatch: `f() /@x` was documented as remove-then-assign, but the compiler parsed `/@x` as bare `/` followed by `@x`, so it popped the replacement value and left `x` unchanged. This matters because LLM-authored Edict needs compact, predictable mutation idioms.

## Acceptance Criteria
- [x] `/@name` applies `/` then `@` to `name`, not bare-pop then assignment.
- [x] `[x]@a [y]/@a a` returns `y`.
- [x] Multi-remove chains such as `[x]@a [y]@a [z]@a //a a` return `x`.
- [x] Intermediate `/` in a prefix chain preserves the live dictionary entry until the chain completes.
- [x] Terminal `/name` preserves legacy remove-and-cleanup behavior.
- [x] Existing `^name^` splice syntax still works.
- [x] LLM guide and language reference document the corrected semantics.
- [x] Durable regression coverage protects the behavior.

## Implementation Notes
- `edict/edict_compiler.cpp` now parses contiguous prefix sigil chains (`@`, `/`, `^`) followed by one no-space identifier as one chain and emits operations left-to-right for the same identifier.
- Added `VMOP_REMOVE_HEAD` for intermediate remove operations in a chain. It pops the current binding head without recursive cleanup, so a following `@` can assign onto the same live dictionary item.
- Added `Cursor::removeHeadOnly()` to support the non-cleaning remove operation.
- `^name^` remains compatible: the first `^name` pushes the destination label, and the terminal bare `^` still emits `SPLICE`.

## Validation
- `cmake --build build --target edict_tests -j2`
- `./build/edict/edict_tests --gtest_filter='EdictVM.*'`
- `cmake --build build --target cpp_agent_tests -j2`
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictLlmModuleTest.*:EdictAgentRootTest.*:AgentRootVmOpsTest.*'`

## Live Examples
```edict
[x]@a [y]/@a a print
-- y
```

```edict
[x]@a [y]@a [z]@a //a a print
-- x
```

```edict
[] @collector [^collector^] @capture capture([one] [two]) collector to_json ! print
-- ["two","one"]
```
