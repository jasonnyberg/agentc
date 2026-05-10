# Goal: G077 — Local LLM Demo Request Diagnostics

**Status**: COMPLETE  
**Created**: 2026-05-10  
**Completed**: 2026-05-10

## Objective
Diagnose why `./demo_local_llm.sh` failed against the local OpenAI-compatible model even though direct `curl` requests to `http://localhost:8081/v1/chat/completions` succeeded.

## Rationale
The runtime-backed demo was masking the real failure mode. We needed to see the exact outbound request and compare the runtime path against the known-good local server behavior.

## Acceptance Criteria
- [x] Reproduce the failing demo path.
- [x] Dump the exact outbound request shape used by the OpenAI-compatible runtime provider.
- [x] Identify the concrete runtime-side bug or bugs causing the failure.
- [x] Verify `./demo_local_llm.sh` completes successfully against the local model.

## Result
- The first failure was transport-level: `cpp-agent/runtime/common/http_client.cpp` imposed a hard `CURLOPT_TIMEOUT` of 30 seconds on SSE streaming requests, which caused slow local generations to fail as `OpenAI-compatible request failed`.
- The second failure was request-shaping: `cpp-agent/runtime/core/runtime.cpp` only read `messages[*].text` and dropped `messages[*].content`, so the actual user prompt was being sent as an empty string.
- Added explicit outbound request logging in `cpp-agent/runtime/providers/openai/openai_provider.cpp` plus curl error diagnostics in `cpp-agent/runtime/common/http_client.cpp`.
- Verified the final outbound request includes the system prompt and user prompt, and `./demo_local_llm.sh` now returns `The capital of France is **Paris**.`
