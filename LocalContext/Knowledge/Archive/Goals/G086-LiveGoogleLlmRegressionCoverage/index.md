# Goal: G086 — Live Google LLM Regression Coverage

**Status**: COMPLETE  
**Created**: 2026-05-13  
**Completed**: 2026-05-13

## Objective
Add a lightweight opt-in live Google/Gemma regression test that exercises the Edict `llm.init(...)` provider path against real Google LLM servers when credentials are available.

## Rationale
The mock LLM tests prove the Edict/provider/runtime wiring shape, but they do not catch regressions in the real Google transport/auth/model path. A very small `gemma-4-31b-it` request can provide early signal while skipping cleanly in environments without `GEMINI_API_KEY` or `GOOGLE_API_KEY`.

## Acceptance Criteria
- [x] Add a `cpp_agent_tests` gtest that checks for `GEMINI_API_KEY` or `GOOGLE_API_KEY` and skips when neither is present.
- [x] Exercise the real `libagent_runtime.so` path through Edict `llm.init([gemma-4-31b-it])`.
- [x] Assert a deterministic lightweight response (`ok`) from a minimal prompt.
- [x] Build and run the focused LLM test suite.

## Implementation Plan
- [x] Extend `cpp-agent/tests/edict_llm_module_test.cpp` with live-runtime bootstrap helpers.
- [x] Add the credential-gated live Google/Gemma smoke test.
- [x] Validate locally with credentials present.

## Implementation Notes — 2026-05-13
- Added `bootstrapJsonForLiveRuntime()` using `build/cpp-agent/libagent_runtime.so` instead of the mock runtime.
- Added `hasGoogleApiKey()` so the live test skips cleanly without `GEMINI_API_KEY` or `GOOGLE_API_KEY`.
- Added `EdictLlmModuleTest.LiveGoogleGemmaRequestReturnsOkWhenCredentialsArePresent`, which loads the Edict LLM modules, configures live runtime bindings, initializes `llm.init([gemma-4-31b-it])`, sends a minimal deterministic prompt, and asserts the last stdout line is `ok`.

## Validation — 2026-05-13
- `cmake --build build --target cpp_agent_tests -j2`
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictLlmModuleTest.LiveGoogleGemmaRequestReturnsOkWhenCredentialsArePresent'` — passed 1/1 with live credentials present.
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictLlmModuleTest.*'` — passed 9/9, including the new live Google/Gemma test.
