# Timeline: 2026-03-21

## G019 — SlabId LTV Type Unification — Complete

Eliminated `typedef void* LTV` → `typedef uint32_t SlabId` / `typedef SlabId LTV`.
Folded `libboxing.so` into `libcartographer.so` via thin C++ wrapper (`boxing_export.cpp`).
Added `CPtr<T>::release()` to `core/alloc.h`.

Key changes:
- `ltv_api.h`: new `LTV` typedef + boundary utilities (`cptr_to_ltv`, `ltv_borrow`, `ltv_adopt`)
- `ffi.cpp`: `FFI_TYPE_UINT32` for LTV passthrough (was `FFI_TYPE_POINTER`)
- `cartographer/CMakeLists.txt`: added `boxing_export.cpp`; removed `boxing` shared lib target
- Deleted `cartographer/libboxing/` directory entirely
- 7/7 suites pass (90/90 tests); `test_boxing_ffi.sh` 11/11; `demo_boxing.sh` clean

**Related Goal**: [G019-SlabIdLtvUnification](../../Goals/G019-SlabIdLtvUnification/index.md)

---

## G018 Phase D — Remove VM Boxing Opcodes — Complete

Removed `VMOP_BOX`, `VMOP_UNBOX`, `VMOP_BOX_FREE` and their `op_` implementations
from the Edict VM entirely. `createBootstrapCuratedCartographer()` now builds
func-def trees for `cartographer.box/unbox/box_free` routing through `evalDispatchFFI`.

Two bugs fixed:
1. `ffi_type_ltv_handle` had `size=0, alignment=0` → set to `4, 4` (libffi does not
   fill these in for non-struct types; caused `ffi_prep_cif` to fail)
2. SlabId ABI encoding was `(index<<16)|offset` but x86-64 little-endian pair layout
   is `index|(offset<<16)` → fixed encode and decode symmetrically

7/7 suites pass; `test_boxing_ffi.sh` 11/11; `demo_boxing.sh` end-to-end clean.

**Related Goal**: [G018-FfiLtvPassthrough](../../Goals/G018-FfiLtvPassthrough/index.md)

---

## Nested Struct Boxing Support — Complete

Added support for boxing/unboxing C structs containing nested struct fields.

- `demo/demo_complex.h`: new test header with 10 scalar types + `InnerPoint` nested struct
- `agentc_box` / `agentc_unbox` handle nested field recursion via `type_def` child lookup
- `demo/test_boxing_ffi.sh` expanded from 11 to initial nested-struct coverage
- Committed (commit 2754d88)
- 7/7 suites pass; `test_boxing_ffi.sh` passing

---

## G020 — Early Type Binding (`bindTypes()`) — Complete

Added `bindTypes()` post-parse pass to `Mapper::materialize()`. For each field
whose type string is `"struct X"`, the pass resolves `X` in the Mapper namespace
and attaches a `"type_def"` CPtr child to the field node. This eliminates the
`ns` parameter from the entire boxing API.

Changes:
- `cartographer/mapper.h` / `mapper.cpp`: `bindTypes()` private method
- `cartographer/boxing.h` / `boxing.cpp`: `ns` param removed from `box()`, `packStruct()`, `unpackStruct()`
- `cartographer/boxing_ffi.h` / `boxing_export.cpp`: C ABI `agentc_box` reduced to 2 args
- `edict/edict_vm.cpp`: bootstrap func-def tree for `cartographer.box` updated (no `ns` node)
- `cartographer/tests/mapper_tests.cpp`: `BindTypesCreatesTypeDef` test added
- `cartographer/tests/test_input.h`: `Rect` struct (two `Point` fields) added
- `demo/test_boxing_ffi.sh`: expanded to 24 assertions (all pass)
- 7/7 suites pass (90/90 tests)

**Related Goal**: [G020-EarlyTypeBinding](../../Goals/G020-EarlyTypeBinding/index.md)

---

## Cartographer CLI Pipeline — Complete

Full five-stage CLI pipeline built and tested:

```
header.h
  ├── cartographer_dump              → human JSON
  └── cartographer_parse             → parser_json_v1 {"symbols":[...]}
          ├── cartographer_schema_inspect  → human JSON
          └── cartographer_resolve <lib.so>  → resolver_json_v1
                    └── cartographer_resolve_inspect  → human summary
```

Shell regression tests:
- `demo/test_cartographer_dump.sh` — 24/24 assertions pass
- `demo/test_cartographer_schema_inspect.sh` — 24/24 assertions pass
- `demo/test_cartographer_resolve_inspect.sh` — 22/22 assertions pass

Each test script follows the `assert_eq` / `assert_contains` helper pattern with
`PASS`/`FAIL` counters and exits non-zero if any assertion fails.

---

## `agentc.sh` Wrapper Functions — Complete

`agentc.sh` (sourced shell file, not executed directly) now contains:

### Individual wrappers
| Function | Binary |
|---|---|
| `edict` | `build/edict/edict` |
| `cartographer_dump` | `build/cartographer/cartographer_dump` |
| `cartographer_parse` | `build/cartographer/cartographer_parse` |
| `cartographer_schema_inspect` | `build/cartographer/cartographer_schema_inspect` |
| `cartographer_resolve` | `build/cartographer/cartographer_resolve` |
| `cartographer_resolve_inspect` | `build/cartographer/cartographer_resolve_inspect` |
| `agentc_visualize` | `build/agentc_visualize` |

### Pipeline compositions
- `agentc_schema <header.h>` — `cartographer_parse | cartographer_schema_inspect`
- `agentc_resolve <header.h> <lib.so>` — `cartographer_parse | cartographer_resolve | cartographer_resolve_inspect`

### Meta-functions
- `agentc_test` — runs every wrapper with a working example, reports PASS/FAIL (9/9 pass), returns failure count as exit code
- `agentc_demo` — runs every wrapper and prints output under `=== name ===` section headers; uses `_run` helper (prints `  $ cmd` to stderr) for single commands; pipeline stages labeled via `_section`

### Key implementation notes
- `BUILD` path uses `$(dirname "${(%):-%x}")/build` (zsh `${(%):-%x}` = script's own path, correct when sourced; `$0` would be the sourcing shell)
- `set -euo pipefail` is NOT present at top level (would pollute the sourcing shell causing VS Code terminal integration failures: `RPROMPT: parameter not set`)
- All pipeline commands in `agentc_test` / `agentc_demo` use `|| true` to prevent shell exit on non-zero return

---

## Documentation Updates — Complete

- **`README.md`**: test count updated (74→90); `core/` description updated; Cartographer section now includes CLI pipeline diagram and `agentc.sh` mention; status section updated; deleted `doc/AgentC.md` reference removed
- **`LocalContext/Knowledge/WorkProducts/edict_language_reference.md`**: §2 "Running Edict" now has `agentc.sh` subsection
- **`doc/AgentC.md`** deleted — was a pre-implementation design document with inaccurate syntax (wrong rewrite rule syntax, Racket-style miniKanren, wrong Cartographer description); README.md and the language reference supersede it completely
