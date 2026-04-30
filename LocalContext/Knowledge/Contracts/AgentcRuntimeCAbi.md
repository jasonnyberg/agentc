# Contract: AgentC Runtime C ABI

## Purpose
Define the stable native C ABI exposed to Edict's FFI importer for provider/model configuration and normalized LLM request dispatch.

## Scope
This ABI is the **only native surface Edict needs to import** for LLM access. Provider-specific logic remains behind the runtime boundary and is selected from JSON configuration and/or per-request overrides.

## Design Rules
1. **Single Edict-facing ABI**
   - Edict imports one header/shared library surface.
2. **Opaque runtime handle**
   - Native provider/auth/HTTP state stays behind an opaque handle.
3. **JSON in / JSON out**
   - Config, request, trace, and error payloads are UTF-8 JSON strings.
4. **Normalized response envelope**
   - Provider-specific payloads are normalized before returning to Edict.
5. **No agent policy in the ABI**
   - The ABI performs transport/runtime work only, not orchestration.

## Header Contract
Proposed public header: `cpp-agent/include/agentc_runtime/agentc_runtime.h`

```c
#ifndef AGENTC_RUNTIME_H
#define AGENTC_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* agentc_runtime_t;

const char* agentc_runtime_version(void);

agentc_runtime_t agentc_runtime_create_json(const char* config_json);
agentc_runtime_t agentc_runtime_create_file(const char* config_path);

int agentc_runtime_configure_json(agentc_runtime_t runtime, const char* config_json);
int agentc_runtime_configure_file(agentc_runtime_t runtime, const char* config_path);

char* agentc_runtime_request_json(agentc_runtime_t runtime, const char* request_json);
char* agentc_runtime_last_error_json(agentc_runtime_t runtime);
char* agentc_runtime_last_trace_json(agentc_runtime_t runtime);

void agentc_runtime_destroy(agentc_runtime_t runtime);
void agentc_runtime_free_string(char* value);

#ifdef __cplusplus
}
#endif

#endif
```

## Function Semantics

### `const char* agentc_runtime_version(void)`
Returns a static, process-lifetime version string.

**Memory ownership**
- Caller must **not** free the returned pointer.

---

### `agentc_runtime_t agentc_runtime_create_json(const char* config_json)`
Create a runtime instance from a UTF-8 JSON config string.

**Inputs**
- `config_json`: UTF-8 JSON text matching 🔗[AgentcRuntimeJsonContract](./AgentcRuntimeJsonContract.md)

**Returns**
- non-null opaque runtime handle on success
- `NULL` on failure

**Failure reporting**
- If creation fails before a handle exists, failure details are not recoverable from `last_error_json`.
- Creation failures should be limited to malformed config, missing required runtime dependencies, or catastrophic initialization failure.

**Usage guidance**
- Edict should typically create one long-lived runtime handle and reuse it.

---

### `agentc_runtime_t agentc_runtime_create_file(const char* config_path)`
Create a runtime instance from a JSON config file path.

**Inputs**
- `config_path`: UTF-8 path to a JSON config file

**Returns**
- non-null opaque runtime handle on success
- `NULL` on failure

**Notes**
- File loading is a convenience entrypoint only.
- The canonical config format is still JSON.

---

### `int agentc_runtime_configure_json(agentc_runtime_t runtime, const char* config_json)`
Replace or merge runtime configuration from a JSON string.

**Return codes**
- `0` = success
- non-zero = failure

**Failure details**
- On failure, `agentc_runtime_last_error_json(runtime)` must return a structured error envelope.

**Required behavior**
- The runtime must remain usable after a failed configure call.
- Configuration updates must be atomic from the caller perspective.

---

### `int agentc_runtime_configure_file(agentc_runtime_t runtime, const char* config_path)`
Replace or merge runtime configuration from a JSON file.

**Return codes**
- `0` = success
- non-zero = failure

**Failure details**
- On failure, `agentc_runtime_last_error_json(runtime)` must return a structured error envelope.

---

### `char* agentc_runtime_request_json(agentc_runtime_t runtime, const char* request_json)`
Dispatch one normalized request and return one normalized response envelope as JSON.

**Inputs**
- `runtime`: valid runtime handle
- `request_json`: UTF-8 JSON request matching 🔗[AgentcRuntimeJsonContract](./AgentcRuntimeJsonContract.md)

**Returns**
- allocated UTF-8 JSON string
- caller must release it with `agentc_runtime_free_string(...)`
- `NULL` only for catastrophic ABI-level failure (e.g. allocation failure)

**Success behavior**
- On normal success, returned JSON must contain `"ok": true`.

**Logical/provider/config errors**
- These must still return a JSON envelope when possible, with `"ok": false` and structured `error` data.
- `NULL` must be reserved for unrecoverable/native ABI failures only.

**No streaming in v1**
- The initial contract is request/response only.
- Streaming may be added later as a separate ABI extension, not as a silent behavior change.

---

### `char* agentc_runtime_last_error_json(agentc_runtime_t runtime)`
Return the last structured error envelope for this runtime instance.

**Returns**
- allocated UTF-8 JSON string or `NULL` if no error is available
- caller must release it with `agentc_runtime_free_string(...)`

**Purpose**
- debugging
- post-failure inspection for `create/configure/request`

---

### `char* agentc_runtime_last_trace_json(agentc_runtime_t runtime)`
Return the last structured trace/debug envelope for this runtime instance.

**Returns**
- allocated UTF-8 JSON string or `NULL` if no trace is available
- caller must release it with `agentc_runtime_free_string(...)`

**Purpose**
- request/selection debugging
- provider/model resolution diagnostics
- later observability integration

---

### `void agentc_runtime_destroy(agentc_runtime_t runtime)`
Destroy a runtime handle and release all associated native resources.

**Behavior**
- Must accept `NULL` safely.
- Must release provider/runtime/transient resources owned by the handle.

---

### `void agentc_runtime_free_string(char* value)`
Release any string allocated by the runtime ABI.

**Behavior**
- Must accept `NULL` safely.
- Must only be used for pointers returned by ABI string-returning functions.

## Selection Rules
The runtime must select provider/model/resources using the following precedence:

1. **Per-request override**
   - `request.provider`, `request.model`, request-level timeout/options
2. **Runtime config defaults**
   - `default_provider`, `default_model`, runtime-level defaults
3. **Provider-local defaults**
   - provider-specific default model / base URL / credential source

## Error Envelope Contract
All recoverable failures returned through JSON must follow this shape:

```json
{
  "ok": false,
  "request_id": null,
  "provider": null,
  "model": null,
  "error": {
    "code": "config_invalid",
    "message": "Missing default_provider",
    "retryable": false,
    "provider_error": null
  }
}
```

### Required error fields
- `code`
- `message`
- `retryable`

### Recommended error codes
- `config_invalid`
- `config_load_failed`
- `runtime_not_initialized`
- `provider_not_found`
- `model_not_found`
- `auth_missing`
- `auth_refresh_failed`
- `request_invalid`
- `request_timeout`
- `provider_http_error`
- `provider_response_invalid`
- `normalization_failed`
- `internal_error`

## ABI Stability Rules
- Function names and signatures are stable once landed.
- New capabilities should be added by new functions, not by changing existing semantics.
- JSON contracts may grow by additive optional fields, but existing required fields must remain stable.

## Non-Goals
- No direct vendor-specific raw payload contract for Edict.
- No agent loop/orchestration ownership in the ABI.
- No unrestricted model-driven FFI/native execution in the ABI.

## Relationship to Edict
Edict should import this ABI and then expose a higher-level `agentc` module in Edict code or curated bootstrap bindings.

Recommended Edict-facing wrappers:
- `agentc.configure`
- `agentc.call`
- `agentc.last_error`
- `agentc.last_trace`

The Edict layer remains responsible for policy, orchestration, and any later controlled execution behavior.