# Phase 5: Provider Integration

## Goal
Implement the Google Gemini and OpenAI provider logic using libcurl and the SSE parser.

## Acceptance Criteria
- [ ] Google Gemini provider correctly issues POST request with SSE streaming.
- [ ] SSE payload is parsed and converted to `AssistantMessageEvent`.
- [ ] OpenAI/Copilot provider correctly issues POST request with SSE streaming.
- [ ] Authentication headers injected correctly for both providers.

## Implementation Steps
1. Create a `HttpClient` wrapper for libcurl.
2. Update `google.cpp` to use `HttpClient` and `SSEParser`.
3. Update `openai.cpp` to use `HttpClient` and `SSEParser`.
4. Run integration test with actual (API key protected) endpoints.
EOF
,path: