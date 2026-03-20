# G017 — Edict Stdin/File Script Mode

**Status**: Complete  
**Completed**: 2026-03-20  
**Priority**: Medium  

## Objective

Add a non-interactive script execution mode to the edict interpreter, allowing edict code to be piped from stdin or loaded from a file — without the REPL prompt/banner.

## Design

- New public method `EdictREPL::runScript(std::istream& in)` — accepts any `std::istream`, returns `bool` (success/failure).
- Comment support: lines whose first non-whitespace character is `#` are skipped.
- Windows line endings: trailing `\r` stripped before processing.
- Blank lines: skipped silently.
- Error reporting: `Error (line N): <msg>` to stderr on VM_ERROR or exception; returns `false` immediately.
- CLI wiring in `main.cpp`:
  - `edict -` → `runScript(std::cin)`, exits 0 or 1
  - `edict FILE` → opens `std::ifstream`, `runScript(file)`, exits 0 or 1; `Error: cannot open file: FILE` if not found
  - Unknown `-` flags still print usage and exit 2
  - `-e CODE` unchanged

## Files Changed

| File | Change |
|------|--------|
| `edict/edict_repl.h` | Added `#include <istream>`; added `bool runScript(std::istream& in)` to public API |
| `edict/edict_repl.cpp` | Implemented `runScript` before `handleSpecialCommand` |
| `edict/main.cpp` | Added `#include <fstream>`; updated `printUsage`; added `-` (stdin) and positional FILE cases |

## Outcome

- Build: clean
- Stdin mode: `edict -` works with comments, blank lines, assignments
- File mode: `edict script.edict` works; nonexistent file prints error and exits 1
- Unknown flags exit 2 with usage
- 7/7 test suites pass (74/74 tests)
