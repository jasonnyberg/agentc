# Goal: G087 — Prefer Adjacent Edict Eval Sigil

**Status**: COMPLETE  
**Created**: 2026-05-13  
**Completed**: 2026-05-13

## Objective
Update Edict-facing documents, guides, scripts, modules, and examples to prefer adjacent eval sigil spelling (`f!`) rather than the older style where `!` appears as a separate spaced token after the word.

## Rationale
Both forms are accepted by the tokenizer, but user-facing material should present one idiomatic style. Adjacent spelling makes eval visually bind to the word being evaluated and avoids teaching `!` as a separate spaced word.

## Scope
- README.md and user-facing Edict/VM guides.
- Checked-in `.edict`/`.ed` scripts and modules.
- Shell/TypeScript demo snippets that emit or execute Edict.
- Active HRM Edict-facing notes where examples or command snippets would otherwise teach the old style.

## Acceptance Criteria
- [x] README and primary Edict/VM guides use adjacent eval spelling in examples and opcode tables.
- [x] Edict modules and checked-in script files use adjacent eval spelling.
- [x] Demo shell snippets and launcher-injected Edict use adjacent eval spelling.
- [x] Run focused validation that Edict still accepts adjacent-eval forms in modules/tests/demos.

## Implementation Plan
- [x] Inventory old spaced eval occurrences in docs/scripts.
- [x] Apply targeted replacements in Markdown guides and script/module files.
- [x] Update launcher/demo snippets and active HRM state references.
- [x] Validate with build/focused Edict and LLM module tests.

## Implementation Notes — 2026-05-13
- Updated README, the Edict language reference, the LLM-oriented VM guide, active Edict-facing HRM notes, checked-in `.edict`/`.ed` modules/scripts, shell demos, TypeScript integration snippets, and C++ test/demo Edict source strings to prefer adjacent eval spelling.
- Updated VM bootstrap/prelude source strings and comments to use adjacent spelling too.
- Preserved legitimate shell negation forms while inventorying old eval style.
- Fixed `PiSimulationTest.MiniKanrenLogicExample` while validating: it was launching stale `edict/edict` from the source tree, which caused the `> > >` prompt flood. It now launches `TEST_EDICT_BIN_DIR/edict`, imports the Kanren runtime explicitly, and prints the result as JSON.

## Validation — 2026-05-13
- `cmake --build build --target edict_tests -j2`
- `./build/edict/edict_tests --gtest_filter='PiSimulationTest.MiniKanrenLogicExample'` — passed 1/1.
- Focused Edict suite: `EdictVM.*:VMStackTest.*:SimpleAssignTest.*:RewriteLanguageTest.*:RegressionMatrixTest.*:CallbackTest.BootstrapImportCapsuleOwnsImportSurface:CallbackTest.ImportResolvedInjectsDefinitionsAndInvokesImmediately:CallbackTest.InterpretedImportPipelineCanRoundTripThroughJson:CallbackTest.LoadBuiltinReportsMissingLibrary:PiSimulationTest.MiniKanrenLogicExample` — passed 53/53.
- `cmake --build build --target cpp_agent_tests -j2`
- Focused cpp-agent Edict/LLM suite: `EdictAgentcModuleTest.*:EdictHelloLoopTest.*:EdictStatefulLoopTest.*:EdictAgentRootTest.*:EdictLlmModuleTest.*` — passed 19/19, including live Google/Gemma credential-gated coverage.
- `EDICT_AUTO_CHAT=0 ./edict.sh -e 'llm.catalog! to_json! print'` — passed.
- Inventory check: no old spaced eval style remains in non-archived Markdown/Edict/script/TypeScript files; only legitimate shell negation remains.
