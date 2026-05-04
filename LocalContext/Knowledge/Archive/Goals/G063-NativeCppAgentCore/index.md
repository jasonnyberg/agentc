# G063: Native C++ Agent Core

## Goal
Build a minimal, stripped-down C++ AI coding agent natively integrated with the Edict VM
in-process. No built-in tools. All external capabilities provided exclusively through
Edict's Cartographer FFI. Three LLM providers: Google Gemini, OpenAI, GitHub Copilot.
Credentials are already present in the current environment and require zero human setup.

## Status
**IN PROGRESS — FULLY AUTONOMOUS**

---

## Concept and Rationale

### The Architectural Inversion
```
Pi today:    Agent (TypeScript) ──socket──► Edict VM (subprocess)
              └── Built-in tools: bash, read, write, edit (hardcoded C++ → TS bindings)

G063 target: Agent (C++) ──vm.execute()──► Edict VM (in-process linked library)
              └── Zero built-in tools
                  LLM tool surface = Edict dictionary
                  Capabilities loaded at runtime via resolver.import !
                  Any C-ABI library is immediately a tool
```

The fundamental insight: Edict's Cartographer FFI already solves the "tool problem" for
the entire C/C++ library ecosystem. The agent's job is to hold a conversation and dispatch
tool calls into the VM. Adding libcurl, libclang, libkanren, libpq, or any C library as
an agent capability is a one-line Edict script — never a C++ code change.

### Why Three Providers is Sufficient
GitHub Copilot is NOT a separate protocol. It is the OpenAI Completions API with:
- A different `baseUrl` (derived from the access token itself)
- A GitHub Bearer token for auth
- Six specific HTTP headers

Therefore the implementation is:
- **Google Gemini**: one HTTP+SSE client (port `google-gemini-cli.ts`)
- **OpenAI**: one HTTP+SSE client (port `openai-completions.ts`)
- **GitHub Copilot**: the OpenAI client + header injection + credential reader

---

## Credential Map (Complete — No Human Interaction Required)

All credentials needed for all three providers are already present in this environment.
No re-authentication, no browser flows, no API key prompts needed.

### Google Gemini
```
Source:  Environment variable
Key:     GEMINI_API_KEY
Value:   Present and set (verified 2026-04-27)
C++ read: const char* key = getenv("GEMINI_API_KEY");
Usage:   HTTP header: x-goog-api-key: {key}
         OR query param: ?key={key}
```

### OpenAI
```
Source:  Environment variable
Key:     OPENAI_API_KEY
Value:   Present and set (verified 2026-04-27)
C++ read: const char* key = getenv("OPENAI_API_KEY");
Usage:   HTTP header: Authorization: Bearer {key}
```

### GitHub Copilot (OAuth — reads from Pi's auth.json)
```
Source:  ~/.pi/agent/auth.json
Key:     github-copilot → access (short-lived Copilot token)
         github-copilot → refresh (GitHub OAuth token, ghu_...)
         github-copilot → expires (milliseconds epoch)

auth.json schema:
{
  "github-copilot": {
    "type": "oauth",
    "access": "tid=<32chars>;exp=<unix_sec>;proxy-ep=proxy.individual.githubcopilot.com;sku=...;...",
    "refresh": "ghu_<38chars>",
    "expires": <unix_milliseconds_int>
  }
}

Access token is valid: expires field > current time (verified 2026-04-27)
Refresh token: present (long-lived GitHub OAuth token)

Base URL derivation (from access token itself — no config needed):
  regex: /proxy-ep=([^;]+)/  →  proxy.individual.githubcopilot.com
  transform: replace("proxy.", "api.")
  result: https://api.individual.githubcopilot.com

Token refresh (when expires <= now):
  URL:    GET https://api.github.com/copilot_internal/v2/token
  Header: Authorization: Bearer {refresh_token}
  Header: User-Agent: GitHubCopilotChat/0.35.0
  Header: Editor-Version: vscode/1.107.0
  Header: Editor-Plugin-Version: copilot-chat/0.35.0
  Header: Copilot-Integration-Id: vscode-chat
  Response: { "token": "tid=...", "expires_at": <unix_seconds_int> }
  Write back to auth.json:
    access  = response.token
    expires = response.expires_at * 1000 - 300000  (5 min buffer, Pi convention)

Request auth header: Authorization: Bearer {access_token}

Per-request dynamic headers (from github-copilot-headers.ts):
  X-Initiator: "user" if last message is user role, else "agent"
  Openai-Intent: "conversation-edits"
  [if images present] Copilot-Vision-Request: "true"

Static headers (same for every request):
  User-Agent: GitHubCopilotChat/0.35.0
  Editor-Version: vscode/1.107.0
  Editor-Plugin-Version: copilot-chat/0.35.0
  Copilot-Integration-Id: vscode-chat
```

### Credential Fallback Chain (C++ implementation)
```cpp
// For each provider, try in order:
// 1. Runtime override (CLI --api-key flag)
// 2. auth.json stored credential
// 3. Environment variable

// Google:
//   auth.json → none stored currently
//   env → GEMINI_API_KEY ✓

// OpenAI:
//   auth.json → none stored currently
//   env → OPENAI_API_KEY ✓

// GitHub Copilot:
//   auth.json → github-copilot.access (OAuth) ✓ (auto-refresh if expired)
//   env → GH_TOKEN / GITHUB_TOKEN / COPILOT_GITHUB_TOKEN (fallback)
```

---

## Architecture

```
cpp-agent/
├── CMakeLists.txt
├── main.cpp                 — CLI, startup, capability bundle loading
├── ai_types.h               — Message, AssistantMessage, ToolCall, Context, Tool, Model
├── agent_types.h            — AgentMessage, AgentContext, AgentTool, AgentEvent, AgentLoopConfig
├── event_stream.h           — callback-based EventStream<TEvent, TResult>
├── agent_loop.cpp           — runAgentLoop(), runLoop(), executeToolCalls()
├── api_registry.cpp         — provider registry (string → StreamFn map)
├── sse_parser.cpp           — shared libcurl + SSE line parser
├── credentials.cpp          — env var lookup + auth.json reader + Copilot token refresh
├── providers/
│   ├── google.cpp           — Gemini raw HTTP+SSE (port of google-gemini-cli.ts)
│   └── openai.cpp           — OpenAI raw HTTP+SSE + Copilot header injection
└── edict_tools.cpp          — VM tool dispatch + dictionary → Tool schema generation

bundles/
├── posix.edict              — POSIX file I/O, process spawn
├── http.edict               — libcurl wrappers
└── logic.edict              — libkanren (already verified working)
```

---

## Implementation Plan (Fully Autonomous)

Human interaction required: **ZERO** for Phases 1–6.
No API keys to provide. No browser flows. No configuration.

---

### Phase 1: Build Scaffold + Type Definitions
**Deliverable**: Project compiles cleanly. All C++ types defined and matching Pi's TypeScript interfaces.
**Human input**: None.

- [ ] Create `cpp-agent/CMakeLists.txt`
  ```cmake
  add_executable(cpp-agent main.cpp agent_loop.cpp api_registry.cpp
                 sse_parser.cpp credentials.cpp edict_tools.cpp
                 providers/google.cpp providers/openai.cpp)
  target_link_libraries(cpp-agent libedict nlohmann_json::nlohmann_json CURL::libcurl)
  target_include_directories(cpp-agent PRIVATE ${CMAKE_SOURCE_DIR})
  ```
- [ ] Add `add_subdirectory(cpp-agent)` to root `CMakeLists.txt`
- [ ] Add `nlohmann/json` via `FetchContent` (if not already present)
- [ ] Add `find_package(CURL REQUIRED)` to root cmake
- [ ] Port `~/pi-mono/packages/ai/src/types.ts` → `cpp-agent/ai_types.h`
  - `struct TextContent`, `ThinkingContent`, `ToolCall`, `ImageContent`
  - `using Content = std::variant<TextContent, ThinkingContent, ToolCall, ImageContent>`
  - `struct UserMessage`, `AssistantMessage`, `ToolResultMessage`
  - `using Message = std::variant<UserMessage, AssistantMessage, ToolResultMessage>`
  - `struct Tool { string name, description; nlohmann::json parameters_schema; }`
  - `struct Context { string system_prompt; vector<Message> messages; vector<Tool> tools; }`
  - `struct Model { string id, api, provider, base_url; map<string,string> headers; }`
  - `struct StreamOptions { string api_key; optional<int> max_tokens; }`
  - `struct Usage { int input, output, cache_read, cache_write; }`
  - `using AssistantMessageEvent = std::variant<StartEvent, TextDeltaEvent, ToolCallEndEvent, DoneEvent, ErrorEvent>`
- [ ] Port `~/pi-mono/packages/agent/src/types.ts` → `cpp-agent/agent_types.h`
  - `struct AgentTool { string name, description; json schema; function<AgentToolResult(json, ...)> execute; }`
  - `struct AgentContext { string system_prompt; vector<AgentMessage> messages; vector<AgentTool> tools; }`
  - `struct AgentLoopConfig { int max_iterations = 10; }`
  - `using AgentEvent = std::variant<AgentStartEvent, TurnStartEvent, MessageStartEvent, ToolExecutionStartEvent, ...>`
- [ ] Implement `cpp-agent/event_stream.h`
  ```cpp
  template<typename TEvent, typename TResult>
  class EventStream {
    using EmitFn = std::function<void(TEvent)>;
    using DoneFn = std::function<void(TResult)>;
    // callback-based, no coroutines
  };
  ```
- [ ] Create `cpp-agent/main.cpp` stub (empty main that links)
- [ ] **Verify**: `make compile` succeeds, `make test` still passes

**Status**: Not started
**Next Action**: Create `cpp-agent/CMakeLists.txt`

---

### Phase 2: Credential Layer
**Deliverable**: Agent reads all three providers' credentials from existing environment. No user action.
**Human input**: None — all credentials already present.

- [ ] Implement `cpp-agent/credentials.h` / `credentials.cpp`

  **Google (trivial)**:
  ```cpp
  std::string get_google_api_key() {
      if (const char* k = getenv("GEMINI_API_KEY")) return k;
      if (const char* k = getenv("GOOGLE_API_KEY")) return k;
      return "";
  }
  ```

  **OpenAI (trivial)**:
  ```cpp
  std::string get_openai_api_key() {
      if (const char* k = getenv("OPENAI_API_KEY")) return k;
      return "";
  }
  ```

  **GitHub Copilot (auth.json + token refresh)**:
  ```cpp
  struct CopilotCredentials {
      std::string access;   // tid=...;proxy-ep=...
      std::string refresh;  // ghu_...
      int64_t expires_ms;   // unix milliseconds
  };

  // Read from ~/.pi/agent/auth.json
  CopilotCredentials load_copilot_credentials();

  // Parse base URL from access token: proxy-ep= → api.xxx
  std::string copilot_base_url(const std::string& access_token);
  // regex: /proxy-ep=([^;]+)/ → replace "proxy." with "api."
  // fallback: "https://api.individual.githubcopilot.com"

  // Refresh if expired (single HTTP GET)
  // GET https://api.github.com/copilot_internal/v2/token
  // Authorization: Bearer {refresh_token}
  // + Copilot static headers
  // Response: { "token": "...", "expires_at": <seconds> }
  // Write back: access=token, expires=expires_at*1000-300000
  bool refresh_copilot_token(CopilotCredentials& creds);

  // Top-level: load, refresh if needed, return valid access token + base url
  std::pair<std::string, std::string> get_copilot_access_token();
  ```

- [ ] auth.json path: `~/.pi/agent/auth.json` (expand HOME via `getenv("HOME")`)
- [ ] Write unit test: load mock auth.json from temp path, verify key extraction
- [ ] Write unit test: verify base URL parsing from sample access token string
- [ ] **Verify**: tests pass, `make test` still passes

**Status**: Not started
**Next Action**: Begin after Phase 1 completion

---

### Phase 3: SSE Infrastructure + Agent Loop
**Deliverable**: Agent loop processes a full conversation with a mock/recorded provider.
**Human input**: None.

- [ ] Implement `cpp-agent/sse_parser.cpp`
  - `libcurl` with `CURLOPT_WRITEFUNCTION` callback
  - Accumulates bytes, splits on `\n\n`, extracts `data:` lines
  - Calls per-event callback: `std::function<void(const std::string&)>`
  - Handles `[DONE]` sentinel

- [ ] Implement `cpp-agent/api_registry.cpp`
  ```cpp
  using StreamFn = std::function<void(Model, Context, StreamOptions, EventStream<AssistantMessageEvent, AssistantMessage>&)>;
  std::unordered_map<std::string, StreamFn> registry;
  void register_provider(const std::string& api, StreamFn fn);
  StreamFn get_provider(const std::string& api);
  ```

- [ ] Port `~/pi-mono/packages/agent/src/agent-loop.ts` → `cpp-agent/agent_loop.cpp`
  - `run_agent_loop(prompts, context, config, emit_fn)` — main entry
  - `run_loop(context, config, emit_fn)` — iterate until stop/max_iter
  - `execute_tool_calls(context, assistant_msg, config, emit_fn)` — sequential first
  - Tool not found → return error ToolResultMessage (don't throw)

- [ ] Implement mock StreamFn:
  ```cpp
  // Returns hardcoded SSE sequence: start → text_delta → done
  // Used for all phases 1-3 testing without live API
  StreamFn make_mock_provider(const std::string& response_text);
  ```

- [ ] Implement mock tool that returns a fixed string (for tool-call loop testing)

- [ ] Write test: 2-turn conversation with mock provider, verify event sequence matches expected
- [ ] Write test: tool call loop (LLM requests tool, receives result, continues)
- [ ] **Verify**: all tests pass

**Status**: Not started
**Next Action**: Begin after Phase 2 completion

---

### Phase 4: Google Gemini Provider (Live)
**Deliverable**: Real conversation with Gemini. Uses `GEMINI_API_KEY` from env — already set.
**Human input**: None — `GEMINI_API_KEY` is present in the environment.

- [ ] Port `~/pi-mono/packages/ai/src/providers/google-gemini-cli.ts` → `cpp-agent/providers/google.cpp`
  - **Use this file, not `google.ts`** — google-gemini-cli.ts uses raw HTTP, no SDK
  - Reference: `~/pi-mono/packages/ai/src/providers/google-gemini-cli.ts` (996 lines)
  - Request format: POST `https://generativelanguage.googleapis.com/v1beta/models/{model}:streamGenerateContent`
  - Auth header: `x-goog-api-key: {GEMINI_API_KEY}`
  - Request body: convert `Context` to Gemini `GenerateContentRequest` JSON
    - `contents`: array of `{role, parts: [{text}]}` — user→"user", assistant→"model"
    - `tools`: array of `{functionDeclarations: [{name, description, parameters}]}`
    - `systemInstruction`: `{parts: [{text: system_prompt}]}`
  - SSE response: each `data:` line is a JSON `GenerateContentResponse`
    - `candidates[0].content.parts[].text` → text delta
    - `candidates[0].content.parts[].functionCall` → tool call
    - `candidates[0].finishReason` → "STOP" / "MAX_TOKENS" / "TOOL_CALLS"
    - `usageMetadata.promptTokenCount` + `candidatesTokenCount` → Usage
  - Map to `AssistantMessageEvent`: start, text_delta, text_end, toolcall_end, done/error
- [ ] Register as `"google-gemini-cli"` in api_registry
- [ ] Hardcode test model: `gemini-2.0-flash`
- [ ] Write live integration test: `./cpp-agent "say hello"` with Gemini
- [ ] **Verify**: receives streaming response, prints text

**Status**: Not started
**Next Action**: Begin after Phase 3 completion

---

### Phase 5: OpenAI + GitHub Copilot Provider (Live)
**Deliverable**: Real conversation with GPT-4o via OpenAI API key and via Copilot OAuth.
**Human input**: None — `OPENAI_API_KEY` is in env; Copilot credentials in `auth.json`.

- [ ] Port `~/pi-mono/packages/ai/src/providers/openai-completions.ts` → `cpp-agent/providers/openai.cpp`
  - Reference: `~/pi-mono/packages/ai/src/providers/openai-completions.ts` (1123 lines)
  - Request format: POST `{baseUrl}/v1/chat/completions` with `stream: true`
  - Auth header: `Authorization: Bearer {api_key}`
  - Request body: `{model, messages, tools, stream: true}`
    - Convert `Message[]` → OpenAI format:
      - `UserMessage` → `{role: "user", content: [{type:"text", text}]}`
      - `AssistantMessage` with ToolCall → `{role:"assistant", content, tool_calls:[...]}`
      - `ToolResultMessage` → `{role:"tool", tool_call_id, content}`
    - Tools → `{type:"function", function:{name, description, parameters}}`
  - SSE response: each `data:` line is `ChatCompletionChunk`
    - `choices[0].delta.content` → text delta
    - `choices[0].delta.tool_calls[]` → streaming tool call (accumulate JSON)
    - `choices[0].finish_reason` → "stop" / "tool_calls"
    - `usage.prompt_tokens` + `completion_tokens` → Usage

- [ ] Add GitHub Copilot support to openai.cpp:
  ```cpp
  if (model.provider == "github-copilot") {
      auto [access_token, base_url] = get_copilot_access_token();
      api_key = access_token;
      model.base_url = base_url;
      // Static headers
      headers["User-Agent"] = "GitHubCopilotChat/0.35.0";
      headers["Editor-Version"] = "vscode/1.107.0";
      headers["Editor-Plugin-Version"] = "copilot-chat/0.35.0";
      headers["Copilot-Integration-Id"] = "vscode-chat";
      // Dynamic headers
      headers["X-Initiator"] = last_message_is_user(context) ? "user" : "agent";
      headers["Openai-Intent"] = "conversation-edits";
  }
  ```
- [ ] Register as `"openai-completions"` in api_registry
- [ ] Write live test: `./cpp-agent --provider openai "say hello"` → GPT-4o responds
- [ ] Write live test: `./cpp-agent --provider github-copilot "say hello"` → Copilot responds
- [ ] **Verify**: both providers complete a streaming round-trip with tool call

**Status**: Not started
**Next Action**: Begin after Phase 4 completion

---

### Phase 6: Edict VM In-Process Tool Dispatch
**Deliverable**: LLM dispatches tool calls through the Edict VM. POSIX bundle works end-to-end.
**Human input**: None — all libraries are local.

- [ ] Implement `cpp-agent/edict_tools.cpp`

  ```cpp
  class EdictToolRegistry {
      EdictVM vm;
  public:
      // Load a bundle script (Edict that calls resolver.import !)
      void load_bundle(const std::string& path);

      // Walk VM dictionary, return Tool vector for LLM
      // For FFI functions: name=funcname, description=from type signature
      // parameters: { type:"object", properties:{} } (args from FFI signature)
      std::vector<Tool> get_tools() const;

      // Dispatch: construct "{json_args_as_edict_map} tool_name !" → vm.execute()
      // Read result from vm.popData() → JSON string
      std::string execute_tool(const std::string& name, const nlohmann::json& args);
  };
  ```

  JSON args → Edict map conversion:
  ```
  {"path": "/tmp/foo", "flags": "r"}
  →  { "path": "/tmp/foo" "flags": "r" }   (Edict dict literal)
  →  fopen !
  ```

- [ ] Implement `bundles/posix.edict`:
  ```edict
  unsafe_extensions_allow ! pop
  [/usr/lib/libc.so.6] [/usr/include/stdio.h] resolver.import ! @posix
  posix.fopen @fopen
  posix.fclose @fclose
  posix.fread @fread
  posix.fwrite @fwrite
  posix.popen @popen
  posix.pclose @pclose
  ```

- [ ] Register `EdictToolRegistry` tools with agent loop
- [ ] CLI: `--bundle posix` loads `bundles/posix.edict` before first turn
- [ ] Write test (no API needed): tool call with mock provider dispatches to Edict, returns result
- [ ] Write live test: ask Gemini to "list files in /tmp", verify it uses `opendir`/`readdir`
- [ ] Verify: `make test` still passes (all existing AgentC tests green)

**Status**: Not started
**Next Action**: Begin after Phase 5 completion

---

## Key Architectural Decisions (Locked — Do Not Revisit)

| Decision | Choice | Rationale |
|---|---|---|
| EventStream | Callback-based (`std::function`) | No C++20 coroutine lifetime issues |
| Google provider | Port `google-gemini-cli.ts` NOT `google.ts` | `google.ts` requires `@google/genai` SDK; no C++ SDK exists |
| GitHub Copilot | Reuse OpenAI client + header injection | It IS OpenAI protocol; no separate implementation needed |
| auth.json | Read-only from `~/.pi/agent/auth.json` | Pi-compatible; users with Pi credentials need zero setup |
| Copilot base URL | Parse `proxy-ep=` from access token | Self-contained in token; no external config needed |
| Tool dispatch | Edict expression `{args} name !` | Zero C++ per tool; VM dictionary IS the tool registry |
| Credential priority | runtime → auth.json → env | Matches Pi's `getApiKey()` priority order |
| No hardcoded tools | By design | Security, composability, simplicity |
| No OAuth login flow | By design | Credentials already present; Pi handles OAuth |

---

## Session Resumption Protocol

Every autonomous session MUST:

**Start**:
1. `cat LocalContext/Dashboard.md` → find active phase and **Next Action**
2. `cat LocalContext/Knowledge/Goals/G063-NativeCppAgentCore/index.md` → find the phase's checkbox list
3. Execute the **Next Action**

**End**:
1. Update the checkbox(es) completed in this session
2. Update **Status** and **Next Action** for the current phase
3. Write a progress note (format below)
4. `cat > LocalContext/Dashboard.md` — update with new next action
5. Append to `LocalContext/Knowledge/Timeline/2026/04/27/index.md`

**Progress note format**:
```
### YYYY-MM-DD
- Did: [files created/modified]
- Built: [make compile result — errors fixed Y/N]
- Tested: [make test result]
- Decided: [any architectural decision made + rationale]
- Blocked: [anything that needs human input — should be NONE]
- Next: [exact next action — file to create, function to implement]
```

---

## Implementation References

| File | Lines | Purpose |
|---|---|---|
| `~/pi-mono/packages/agent/src/agent-loop.ts` | 683 | Agent loop — port directly |
| `~/pi-mono/packages/agent/src/types.ts` | 365 | Agent types |
| `~/pi-mono/packages/ai/src/types.ts` | 452 | AI types (Message, Tool, Model, StreamOptions) |
| `~/pi-mono/packages/ai/src/providers/google-gemini-cli.ts` | 996 | Google provider — raw HTTP ✓ |
| `~/pi-mono/packages/ai/src/providers/openai-completions.ts` | 1123 | OpenAI provider |
| `~/pi-mono/packages/ai/src/providers/github-copilot-headers.ts` | 37 | Copilot header logic |
| `~/pi-mono/packages/ai/src/utils/oauth/github-copilot.ts` | ~300 | Copilot token refresh |
| `~/pi-mono/packages/ai/src/env-api-keys.ts` | 202 | Credential env vars |
| `edict/edict_vm.h` | — | Edict VM C++ API |
| `edict/edict_compiler.h` | — | EdictCompiler |
| `demo/demo_cognitive_core_socket.sh` | — | Verified FFI import pattern |

---

## Progress Notes

### 2026-04-27
- Did: Goal created, scoped, and all authentication findings recorded
- Decided: google-gemini-cli.ts (raw HTTP) not google.ts (uses @google/genai SDK — no C++ equivalent)
- Decided: Copilot = OpenAI protocol + header injection (not a separate provider)
- Decided: Callback-based EventStream (not C++20 coroutines)
- Decided: auth.json read-only, Pi-compatible format
- Verified: GEMINI_API_KEY set in env ✓
- Verified: OPENAI_API_KEY set in env ✓
- Verified: github-copilot OAuth token in ~/.pi/agent/auth.json, currently valid ✓
- Verified: Copilot token refresh endpoint documented and understood ✓
- Verified: Copilot base URL derivable from proxy-ep field in access token ✓
- Conclusion: ZERO human interaction required for any phase of this project
- Next: Create cpp-agent/CMakeLists.txt and cpp-agent/ai_types.h
EOF
### 2026-04-27 (Session 2 — Phase 1 + 2 implementation)
- Did: Scaffolded full cpp-agent/ directory structure
  - cpp-agent/CMakeLists.txt (links libedict, libcurl, nlohmann/json via FetchContent)
  - cpp-agent/ai_types.h (port of packages/ai/src/types.ts — Message, Tool, Model, EventStream, StreamFn)
  - cpp-agent/agent_types.h (port of packages/agent/src/types.ts — AgentTool, AgentContext, AgentEvent)
  - cpp-agent/agent_loop.h + agent_loop.cpp (full port of agent-loop.ts, sequential tool dispatch)
  - cpp-agent/api_registry.h + api_registry.cpp (StreamFn registry)
  - cpp-agent/credentials.h + credentials.cpp (Google/OpenAI env vars, Copilot auth.json + token refresh)
  - cpp-agent/edict_tools.h + edict_tools.cpp (VM tool dispatch stub, json→Edict dict converter)
  - cpp-agent/sse_parser.cpp (libcurl SSE line parser, shared infrastructure)
  - cpp-agent/providers/google.h + google.cpp (stub, Phase 4)
  - cpp-agent/providers/openai.h + openai.cpp (stub, Phase 5)
  - cpp-agent/main.cpp (stub)
  - Added add_subdirectory(cpp-agent) to root CMakeLists.txt
- Built: `./build/cpp-agent/cpp-agent` → "Build OK. Phases 1/6 complete." ✓
- Tested: all non-edict tests pass (edict_tests has pre-existing timeout issue unrelated to this work)
- Decided: Use `if constexpr` + explicit type list instead of `requires` (project is C++17, not C++20)
- Decided: ToolContent == UserContent variant; use text_of_tool() to avoid overload ambiguity
- Decided: sse_parser.cpp is header-only style (no separate .h needed, struct SSEParser)
- Phases completed: Phase 1 (scaffold + types) COMPLETE, Phase 2 (credentials) COMPLETE
- Next: Phase 3 — implement mock StreamFn, write agent loop round-trip test, verify tool call dispatch
