# Contract: AgentC Runtime JSON Config / Request / Response

## Purpose
Define the JSON payloads exchanged across the AgentC runtime C ABI.

## Design Principles
- JSON is the wire/config shape.
- Provider-specific request/response details are normalized behind the runtime boundary.
- Request payloads may override runtime defaults, but the runtime remains the authority for provider selection and credential resolution.

## 1. Runtime Config JSON

### Recommended operator file
The intended operator-facing configuration file is `agentc-config.json` at the project root (or any alternate path passed via `--config` / `AGENTC_CONFIG`).

### Resolution precedence
1. request-level `provider` / `model`
2. persisted canonical root runtime preferences
3. explicit host CLI flags / env overrides
4. runtime config file (`agentc-config.json`)
5. built-in fallback defaults

### Minimal config
```json
{
  "default_provider": "google",
  "default_model": "gemini-2.5-flash"
}
```

### Recommended config
```json
{
  "default_provider": "google",
  "default_model": "gemini-2.5-flash",
  "defaults": {
    "timeout_ms": 30000,
    "response_mode": "text",
    "include_raw": false
  },
  "providers": {
    "google": {
      "enabled": true,
      "default_model": "gemini-2.5-flash",
      "api_key_env": "GEMINI_API_KEY",
      "base_url": "https://generativelanguage.googleapis.com"
    },
    "openai": {
      "enabled": true,
      "default_model": "gpt-4.1",
      "api_key_env": "OPENAI_API_KEY",
      "base_url": "https://api.openai.com"
    },
    "github-copilot": {
      "enabled": true,
      "default_model": "gpt-4o",
      "auth_json_path": "~/.pi/agent/auth.json"
    }
  },
  "policy": {
    "allow_raw_response": false,
    "max_request_bytes": 1048576,
    "max_timeout_ms": 120000
  }
}
```

### Required fields
- none strictly required if the caller always supplies provider/model in each request

### Practically required fields for a reusable runtime
- `default_provider`
- `default_model`
- `providers`

### Config semantics
- `default_provider`: fallback provider when request omits `provider`
- `default_model`: fallback model when request omits `model`
- `defaults`: runtime-wide request defaults
- `providers`: provider-specific configuration blocks
- `policy`: runtime-side safety limits for request execution

### Provider block semantics
Each provider block may define:
- `enabled`
- `default_model`
- `base_url`
- credential hints such as:
  - `api_key`
  - `api_key_env`
  - `auth_json_path`
  - provider-specific refresh/config details
- provider-specific optional settings

## 2. Request JSON

### Minimal request
```json
{
  "prompt": "Say hello"
}
```

### Full request
```json
{
  "request_id": "optional-client-request-id",
  "provider": "google",
  "model": "gemini-2.5-pro",
  "system": "You are a careful assistant.",
  "prompt": "Summarize the current task.",
  "messages": [
    { "role": "user", "text": "Previous message" }
  ],
  "response_mode": "text",
  "options": {
    "temperature": 0.2,
    "max_output_tokens": 1024,
    "timeout_ms": 30000,
    "include_raw": false
  },
  "metadata": {
    "session": "local-repl"
  }
}
```

### Request fields
- `request_id` — optional caller-supplied identifier
- `provider` — optional provider override
- `model` — optional model override
- `system` — optional system prompt
- `prompt` — primary text prompt for single-turn requests
- `messages` — optional normalized message history
- `response_mode` — one of:
  - `text`
  - `json`
  - `tool`
- `options` — request-level overrides
- `metadata` — caller-owned opaque metadata for traceability

### Request rules
- At least one of `prompt` or `messages` must be provided.
- `provider` and `model` are optional if config defaults resolve them.
- Request-level values override runtime defaults.
- Unknown additive fields may be ignored unless marked required by a later version.

## 3. Message JSON

### Normalized message shape
```json
{
  "role": "user",
  "text": "Hello"
}
```

### Allowed roles
- `system`
- `user`
- `assistant`
- `tool`

### Optional future fields
- `name`
- `tool_call_id`
- `content`
- `attachments`

## 4. Response JSON

### Success envelope
```json
{
  "ok": true,
  "request_id": "req-123",
  "provider": "google",
  "model": "gemini-2.5-pro",
  "finish_reason": "stop",
  "message": {
    "role": "assistant",
    "text": "Hello world"
  },
  "tool_calls": [],
  "usage": {
    "input_tokens": 12,
    "output_tokens": 4,
    "cache_read_tokens": 0,
    "cache_write_tokens": 0
  },
  "error": null,
  "trace": {
    "selected_provider": "google",
    "selected_model": "gemini-2.5-pro"
  },
  "raw": null
}
```

### Failure envelope
```json
{
  "ok": false,
  "request_id": "req-123",
  "provider": "google",
  "model": "gemini-2.5-pro",
  "finish_reason": "error",
  "message": null,
  "tool_calls": [],
  "usage": null,
  "error": {
    "code": "provider_http_error",
    "message": "HTTP 401 from provider",
    "retryable": false,
    "provider_error": {
      "status": 401
    }
  },
  "trace": {
    "selected_provider": "google",
    "selected_model": "gemini-2.5-pro"
  },
  "raw": null
}
```

### Required response fields
- `ok`
- `request_id`
- `provider`
- `model`
- `finish_reason`
- `message`
- `tool_calls`
- `error`

### Response field semantics
- `ok` — success/failure indicator
- `request_id` — caller or runtime generated id
- `provider` — provider actually selected
- `model` — model actually selected
- `finish_reason` — normalized completion status
- `message` — normalized final assistant message, if any
- `tool_calls` — normalized tool-call intents, if any
- `usage` — normalized token accounting, if available
- `error` — structured error object on failure, otherwise `null`
- `trace` — optional selection/debug data
- `raw` — optional provider-native payload if explicitly enabled

### Allowed `finish_reason` values
- `stop`
- `tool_use`
- `length`
- `error`
- `unknown`

## 5. Tool Call JSON

### Shape
```json
{
  "id": "tool-call-1",
  "name": "search_memory",
  "arguments": {
    "query": "user preference"
  }
}
```

### Purpose
The runtime may normalize provider-native tool-call objects into this stable shape, but the runtime does not execute them.

## 6. Raw Payload Policy
- `raw` is optional and should default to `null`.
- It is included only when explicitly requested by config/request and allowed by runtime policy.
- Edict should not depend on `raw` for canonical behavior.

## 7. Selection / Override Rules
Provider/model selection precedence:
1. request-level override
2. runtime config defaults
3. provider-local defaults

Timeout/option selection precedence:
1. request `options`
2. config `defaults`
3. provider-local implementation defaults
4. hard runtime caps from config `policy`

## 8. Edict Consumption Rules
The Edict layer should treat the response envelope as **data first**:
- inspect `ok`
- inspect `message` / `tool_calls` / `error`
- decide in Edict what happens next
- do not treat provider output as executable by default

## 9. Versioning Rule
The schema is additive-forward-compatible:
- existing required fields remain stable
- new optional fields may be added
- incompatible meaning changes require a version bump in the runtime library