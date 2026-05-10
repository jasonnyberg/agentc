# Goal: G083 — Edict Tail-Prefixed Dictionary History

**Status**: COMPLETE  
**Created**: 2026-05-10  
**Completed**: 2026-05-10

## Objective
Investigate and fix the `-name` tail-selector semantics for Edict dictionary item value histories so lookup, assignment, removal, and prefix chains can operate on the tail/oldest binding.

## Rationale
Edict/Listree dictionary items are backed by CLL value histories. The language intent is that a leading `-` on an identifier selects the tail of that item's value history, enabling queue/dequeue-style use. This must work uniformly for `-a`, `@-a`, `/-a`, and combined prefix chains such as `/@-a`.

## Findings
- Tail lookup already flowed through `Cursor::resolve(...)` and could read the tail in cases like `-a`.
- Tail assignment was broken for simple keys because `op_ASSIGN` used a direct `addNamedItem(...)` fast path for non-dotted keys, bypassing `Cursor::resolve(...)` and treating `-a` as a literal key.
- Tail removal was incomplete because `Cursor::resolve(...)` did not preserve whether the current item was selected from the head or tail for later `assign()` / `remove()` operations.

## Acceptance Criteria
- [x] `-a` reads the tail/oldest value of dictionary item `a`.
- [x] `[t]@-a` appends `t` at the tail of dictionary item `a`.
- [x] `/-a` removes the tail value of dictionary item `a`.
- [x] `[t]/@-a` replaces the tail value of dictionary item `a`.
- [x] Exhaustive tail removals clean up the dictionary item.
- [x] Regression coverage captures the behavior.
- [x] LLM guide and Edict language reference document the semantics.

## Implementation Notes
- Added `Cursor::currentItemFromEnd` to remember whether the current item was resolved through head or tail selection.
- `Cursor::assign(...)`, `Cursor::remove(...)`, and `Cursor::removeHeadOnly()` now use that selected end.
- `op_ASSIGN` now only uses the direct non-dotted fast path for plain names; names with leading `-`, leading `*`, or trailing `-` route through `Cursor` so selector syntax is honored.

## Validation
- Live checks:
  - `[x]@a [y]@a [z]@a -a print` → `x`
  - `[x]@a [y]@a [z]@a [t]@-a -a print` → `t`
  - `[x]@a [y]@a [z]@a /-a -a print` → `y`
  - `[x]@a [y]@a [z]@a [t]/@-a -a print` → `t`
  - `[x]@a [y]@a [z]@a ///-a strict! a to_json ! print` → `null`
- `./build/edict/edict_tests --gtest_filter='EdictVM.*'` — 22/22 passed.
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictLlmModuleTest.*:EdictAgentRootTest.*:AgentRootVmOpsTest.*'` — 15/15 passed.
