# Goal: G102 — Edict Session ID Startup Flag

**Status**: COMPLETE  
**Created**: 2026-05-14  
**Completed**: 2026-05-14

## Objective
Add an Edict startup flag that accepts a session id and maps it to a session-specific storage directory so separate Edict sessions can create/resume their own slab-backed state under a path such as `/tmp/session/<id>/`.

## Rationale
AgentC's persistence direction depends on resumable session state. The Edict executable should expose a user-facing session selector so scripts/launchers can distinguish independent sessions and so future mmap-backed slab files have a stable session namespace.

## Acceptance Criteria
- [x] Edict CLI exposes a startup flag for session id.
- [x] Session id is validated/sanitized to avoid path traversal and unsafe filesystem names.
- [x] Session storage path is created deterministically, defaulting under `/tmp/session/<id>/` unless existing project conventions require another base.
- [x] The selected session path is wired into the VM/runtime persistence configuration where supported today, or recorded as explicit session metadata if authoritative mmap resume is not yet implemented.
- [x] Help/usage and relevant docs/tests are updated.
- [x] Regression coverage verifies flag parsing and session path behavior.

## Implementation Notes — 2026-05-14
- Added raw Edict CLI flags:
  - `--session ID`
  - `--session-id ID`
  - `--session-base DIR`
- Default session base is `EDICT_SESSION_BASE` or `/tmp/session`.
- Session ids are rejected unless they contain only letters, digits, `.`, `-`, and `_`, and are not `.` or `..`.
- The raw Edict executable now uses the existing `SessionStateStore`/`SessionImageStore` path to load a session root before VM construction and save it on normal process exit.
- Session startup strips VM-owned volatile builtins/bootstrap names from restored roots so fresh bytecode thunks are installed by `loadBuiltins()` and stale lossy JSON/null builtin histories do not shadow them.
- Fixed allocator restore sentinel handling: when restored slab images include slab 0 but not the `(0,0)` null sentinel slot, `restoreSlabImages()` now reserves `(0,0)` so the first post-restore allocation does not produce a false/null `CPtr`.
- Added `EdictREPL::getVM()` so CLI session persistence can save after REPL/script/IPC/socket execution.
- Updated README, the Edict language reference, G078/G096 goal context, and persistence facts (`K030_J3_Arena_Persistence`, `ListreeValueSerializationFormat`).

## Validation — 2026-05-14
- `cmake --build build --target edict -j2` — passed.
- Manual raw Edict smoke:
  - `./build/edict/edict --session readme-demo -e "'persisted @answer"`
  - `./build/edict/edict --session readme-demo -e 'answer print'` printed `persisted`.
- Unsafe id smoke: `--session '../escape'` returned exit code 1 with `Invalid session id`.
- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='EdictSessionCliTest.*'` — passed 2/2.
- Focused Edict regression slice including session CLI, miniKanren IPC, and stack discard tests — passed 5/5.
- `cmake --build build --target cpp_agent_tests -j2` — passed.
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='SessionStateStoreTest.*'` — passed 14/14.
- `git diff --check` for touched files — passed.

## Notes
This goal should be scoped as the first practical CLI/session namespace slice. If full authoritative mmap resume is not complete, do not overstate it: wire the session path into current persistence hooks and leave deeper mmap ownership to G096.
