# Goal: G089 — Remove Edict `pop` Keyword

**Status**: COMPLETE  
**Created**: 2026-05-13  
**Completed**: 2026-05-13

## Objective
Update Edict code and documentation to prefer bare `/` for stack discard, with `pop /` patterns normalized to `/ /`, and remove `pop` as a compiler-recognized Edict keyword.

## Rationale
Edict already has the concise bare `/` stack-discard form. Since spaced `/ /` cleanly represents two discards and avoids an English-word alias, user-facing examples and checked-in Edict source should teach the sigil form. Removing `pop` from compiler keywords prevents new code from depending on the old spelling.

## Scope
- Remove `pop` handling from `EdictCompiler::compileIdentifier()`.
- Remove user-facing/core bootstrap `pop` thunk if validation shows no remaining dependency.
- Update checked-in Edict modules/scripts and embedded Edict snippets to use `/` or `/ /`.
- Update README, Edict guides, active HRM notes, and prompt/contracts that teach Edict syntax.
- Preserve non-Edict uses of "pop" in C++ APIs, data-structure comments, and ordinary English where appropriate.

## Acceptance Criteria
- [x] Bare `pop` no longer compiles to `VMOP_POP`.
- [x] Checked-in Edict code uses `/` for single discard and `/ /` for double discard.
- [x] User-facing Edict docs no longer teach `pop` as a keyword.
- [x] Focused Edict/compiler and cpp-agent Edict/LLM tests pass after the transition.
- [x] Search confirms no remaining Edict snippets depend on bare `pop`.

## Implementation Notes — 2026-05-13
- Removed the `identifier == "pop"` compiler keyword case from `EdictCompiler::compileIdentifier()`.
- Removed the bootstrap core builtin thunk named `pop`; `VMOP_POP` remains available through bare `/` only.
- Added `EdictVM.PopIsNotAStackDiscardKeyword`, proving `pop` now resolves as an ordinary symbol when unbound instead of discarding the stack top.
- Updated checked-in Edict modules, scripts, demos, TypeScript integration snippets, C++ embedded Edict strings, REPL help, README, the Edict language reference, the LLM VM guide, active HRM notes, and prompt docs to use `/` or `/ /`.
- Preserved non-Edict C++ API names and ordinary data-structure terminology such as `ListreeValue::get(pop=true)`.

## Validation — 2026-05-13
- `cmake --build build --target edict_tests -j2` — passed; existing unused-variable warnings in callback tests remain.
- `./build/edict/edict_tests --gtest_filter='EdictVM.PopIsNotAStackDiscardKeyword:EdictVM.RemoveTopOfStack:CallIsolationTest.StackIsolation:VMStackTest.*:CallbackTest.Closure:CallbackTest.ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult:CallbackTest.ImportResolvedThreadRuntimeUpdatesSharedCellFromThread:CallbackTest.ImportResolvedThreadRuntimeDirectCapturedMutationDoesNotLeakAcrossThreads'` — passed for the pop-transition-relevant tests selected, excluding known pre-existing callback/parser-map failures.
- Focused Edict style/regression slice including `EdictVM.*`, `VMStackTest.*`, `SimpleAssignTest.*`, `RewriteLanguageTest.*`, `RegressionMatrixTest.*`, selected callback import/thread tests, and `PiSimulationTest.MiniKanrenLogicExample` — passed 59/59.
- `cmake --build build --target cpp_agent_tests -j2` — passed.
- Focused cpp-agent Edict/LLM suite `EdictAgentcModuleTest.*:EdictHelloLoopTest.*:EdictStatefulLoopTest.*:EdictAgentRootTest.*:EdictLlmModuleTest.*` — passed 19/19, including live Google/Gemma coverage in this credentialed environment.
- `./build/edict/edict -e '1 2 / /'` — stack size 0.
- `./build/edict/edict -e "'sentinel pop"` — stack contains `pop` above `sentinel`, confirming `pop` is not a discard keyword.
- `EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.init([gemma-4-31b-it]) @provider provider < [Reply with exactly two lowercase letters: ok] request! > / / provider.assistant_text print'` — returned `ok`.
- `printf 'help\nexit\n' | ./build/edict/edict` shows `/` as stack discard and `/name` as binding removal.
- `git diff --check` — passed.
- Inventory checks: no checked-in `.edict`/`.ed`/`.sh`/`.ts` Edict snippets contain bare `pop`; C++ string-literal scan only finds the intentional negative test and an internal Listree diagnostic.
