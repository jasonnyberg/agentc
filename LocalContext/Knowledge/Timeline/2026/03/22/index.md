# Timeline: 2026-03-22

## Session 2140-2200
🔗[Session Notes](./2140-2200/index.md) — HRM Bootstrap and project state verification.

## Edict Lexer UTF-8 and Double Quotes Fix

Wrote tests for UTF-8 literals and discovered that the previous string migration left the compiler discarding double-quoted strings and blocking Unicode characters in identifiers. 
Updated `edict/edict_compiler.cpp` tokenizer to allow `"` as a valid identifier character (except inside JSON dicts) and added support for high-bit characters `(unsigned char)input[position] >= 0x80` to allow UTF-8 identifiers and words.
Added `demo/test_unicode.ed` and `demo/test_unicode_id.ed` to verify.

---

## G044 — JSON-based Module Import Caching — Complete

Pivoted from G043 (LMDB pickling) to G044 (JSON caching). Implemented a file-based JSON cache in `EdictVM` to bypass the `libclang` parsing bottleneck during module imports. 
The system now writes `resolver_json_v1` strings to `~/.cache/agentc/` upon successful module parsing and resolution, and automatically loads from this cache on subsequent executions. Cache validation is implemented using `mtime` comparisons between the cache file and the source `.h`/`.so` files.
A test wrapper script `demo/demo_import_cache.sh` was added to verify cache hit/miss logic and automated invalidation testing.

---

## G021 — Remove Module Name from Resolver Import API — Complete

Removed the `scopeName` (module name) parameter from the entire resolver import API stack.
The module name was metadata-only and never used for symbol resolution, type binding,
namespace prefixing, or dispatch.

### Files changed (8 tasks)

- **`cartographer/service.h`**: Deleted `std::string scopeName` from `ImportRequest` and `ImportResult` structs; removed `scopeName` param from `importResolvedFile()` (now 1 arg) and `importResolverJson()` (now 2 args)
- **`cartographer/service.cpp`**: Removed all 21 scopeName usages — propagation assignments, all emptiness guards (`if (request.scopeName.empty())`), both `scope`/`requested_name` metadata emissions in `createImportHandleValue` and `materializeImportedDefinitions`
- **`cartographer/protocol.cpp`**: Bumped protocol version `"protocol_v1"` → `"protocol_v2"`; removed `scope` field from wire format in both `encodeImportRequest`/`encodeImportStatus` and `decodeImportRequestFields`/`decodeImportStatus`
- **`edict/edict_vm.cpp`**: Removed `auto scopeValue = popData()`, `std::string scopeName`, and scope validation from all 4 opcodes (`op_IMPORT`, `op_IMPORT_RESOLVED`, `op_IMPORT_DEFERRED`, `op_IMPORT_RESOLVED_JSON`); updated 2 thunks (`resolver.import`, `resolver.import_resolved`) to remove `@scope` binding
- **`cartographer/tests/service_tests.cpp`**: Removed all `request.scopeName = "defs"` setups, all `scope`/`requested_name` metadata assertions, protocol_v1 → protocol_v2 in all 4 assertion sites, both `importResolvedFile(..., "defs")` → 1-arg calls
- **`edict/tests/callback_test.cpp`**: Removed `'defs` argument from all 7 edict source strings that called `resolver.import*`; updated `"protocol_v1"` → `"protocol_v2"`
- **`LocalContext/Knowledge/WorkProducts/edict_language_reference.md`**: Removed `'mylib` from all 4 `resolver.import*` examples (lines ~928, ~942, ~958, ~974)

### Test results (post-G021)

- ctest: 7/7 suites pass
- `demo/test_boxing_ffi.sh`: 24/24
- `demo/test_cartographer_dump.sh`: 24/24
- `demo/test_cartographer_schema_inspect.sh`: 24/24
- `demo/test_cartographer_resolve_inspect.sh`: 22/22

---

## Edict String Literal Migration — Complete

Removed special parsing of standalone double-quoted string literals (`"abc"`) from edict source syntax.
The `TOKEN_STRING` arm was removed from `compileTerm()` in the compiler; instead, `"abc"` is just parsed as a regular word token that includes the double quotes. The new `'word` and `[multi word]` forms are now the correct syntax for pushing string values without surrounding quotes in edict code.
JSON dict syntax (`{ "key": "value" }`) is unaffected.

### Compiler changes

- `edict/edict_compiler.h`: Added `TOKEN_QUOTE` to `TokenType` enum
- `edict/edict_compiler.cpp`:
  - `parseQuoteWord()` implemented — reads identifier characters after `'`
  - `'` branch added to `nextToken()` → emits `TOKEN_QUOTE`
  - `TOKEN_QUOTE` arm added to `compileTerm()` (compiles to `VMOP_PUSHEXT + VMEXT_STRING`, same bytecode as `TOKEN_STRING`)
  - `TOKEN_STRING` arm removed from `compileTerm()` — strings in standalone edict code now produce a compile error (fall through to unknown-token handler)
  - `parseString()` retained — still used by `compileJSONObject` / `compileJSONValue` for JSON dict mode

### Pre-existing bug fix

- `edict/edict_types.h`: `Value()` and `Value(nullptr_t)` now initialize `boolValue(false)` — fixes GCC `-Wmaybe-uninitialized` warning that was pre-existing

### Source migrations (edict files)

All standalone `"string"` literals replaced with `'word` (single token) or `[multi word]` (multi-token) in:
- `demo/demo_ffi_libc.sh`
- `demo/demo_boxing.sh`
- `demo/test_boxing_ffi.sh`
- `demo/demo_cognitive_core.sh`
- `edict/tests/vm_stack_tests.cpp`
- `edict/tests/splice_test.cpp`
- `edict/tests/simple_assign_test.cpp`
- `edict/tests/rewrite_language_test.cpp`
- `edict/tests/cognitive_validation_test.cpp`
- `edict/tests/callback_test.cpp`

`edict/tests/logic_surface_test.cpp` — **not modified** (all strings are inside JSON dict/array literals)

### Test results (post-migration)

- ctest: 7/7 suites pass
- `demo/test_boxing_ffi.sh`: 24/24
- `demo/test_cartographer_dump.sh`: 24/24
- `demo/test_cartographer_schema_inspect.sh`: 24/24
- `demo/test_cartographer_resolve_inspect.sh`: 22/22

### Language reference update

`LocalContext/Knowledge/WorkProducts/edict_language_reference.md` fully updated to reflect new syntax throughout all sections:

- **§4.1 Strings**: new `'word` / `[multi word]` syntax documented; `"abc"` documented as pushing the literal string `"abc"` (including quotes) instead of stripping them
- **§15 Rewrite Rules**: pattern table updated (`'literal`, `'$0`, `'#atom`, `'#list`); mode examples (`'auto`/`'manual`/`'off`); manual-apply and remove examples
- **§18 Cursor**: `"new value"` → `[new value]`
- **§19 FFI**: all path strings and numeric arg strings updated (`'./libmylib.so`, `'10 '32`, `'mylib`, etc.) in load/parse/materialize/JSON-pipeline/deferred/pre-resolved examples
- **§22 Output**: `"hello, world"` → `[hello, world]`; `"42"` → `'42`
- **§24 Opcode table**: `VMOP_REWRITE_MODE` mode column updated
- **Appendix A.1**: `'0 @a`, `'1 @b`, `'10 @n`, `'1 math.sub`
- **Appendix A.2**: `'hello @greeting`, `'world @subject`, `'greet`
- **Appendix A.4**: `'default`, `'primary_result`
- **Appendix A.5**: `'first`, `'second`, `'third`
- **Appendix A.6**: Empty-string pattern (`""`) left as JSON string in pattern arrays; standalone `""` replaced with `[]` for empty list; comment added explaining the distinction
