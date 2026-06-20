# WP — G101 Direct Edict Tool-Emission Path

**Goal**: G101 — Direct Edict Tool-Emission Path
**Status**: Complete
**Date**: 2026-06-20

## Summary

G101 adds the first guarded direct-Edict action path for model-emitted tool use. Curated launcher sessions now expose the `agentc.edict` runtime prerequisites (`ext` and `runtimeffi`) globally, allowing compact model-emitted Edict snippets to invoke a narrow, inspectable action dispatcher without first constructing an outer JSON tool-call loop.

The initial direct-action subset is intentionally small:

```edict
{"op":"read_file","path":"/tmp/example.txt"} agentc_direct_action! @result
result to_json! print
```

This dispatches to the existing `agentc_file_read!` helper and annotates the returned envelope with:

- `emission: "direct-edict-v1"`
- `op: "read_file"`

## Direct Action Contract

### Public API

`agentc_direct_action!` has stack contract:

```text
action -- result
```

where `action` is an Edict object parsed from a compact JSON-shaped literal.

### Allowed MVP Operation

| Operation | Required fields | Behavior |
|---|---|---|
| `read_file` | `path` | Calls `agentc_file_read!`; returns the same content/bytes/truncation semantics as the helper envelope, plus direct-emission metadata. |

### Denied Operations

All non-whitelisted operations return a data envelope instead of executing:

```json
{
  "ok": [],
  "emission": "direct-edict-v1",
  "error": {
    "code": "direct_action_denied",
    "message": "Operation is not in the direct-Edict safe action subset",
    "op": "shell"
  }
}
```

This means model-emitted `shell`, `write_file`, `replace_file`, `resolver.import`, raw runtime FFI, or unknown operations are excluded from the direct-action subset unless a later goal deliberately expands the whitelist with tests.

## Launcher Boundary

`edict.sh` now imports the extension/runtime bindings into the launcher prelude before loading curated modules:

```edict
EDICT_PATH.extensions_library_path EDICT_PATH.extensions_header_path resolver.import! @ext
EDICT_PATH.runtime_library_path EDICT_PATH.runtime_header_path resolver.import! @runtimeffi
```

This satisfies the documented `agentc.edict` prerequisite globally for curated sessions. Provider-scoped tools continue to work as before; the direct-action path is available for compact snippets that do not need a provider object.

## Error / State Safety

- Unsupported operations are represented as ordinary Edict data; they do not call shell/native capability surfaces.
- Read failures remain inspectable through the existing `agentc_file_read!` error envelope.
- The direct action path does not mutate provider conversation/session state.
- The safe subset is data-dispatch only; arbitrary emitted Edict source remains a separate isolation-hardening topic under G100.

## When JSON Envelopes Remain Preferable

Use the existing JSON-envelope helper/tool path instead of direct Edict when:

1. The operation needs a rich schema, nested arguments, or future compatibility with external OpenAI-style tool calls.
2. The action is mutating (`write_file`, `replace_file`) or high-risk (`shell`, native import, process control).
3. Host-side audit/logging, tool-call IDs, or multi-provider interoperability are required.
4. The model output is untrusted free-form code rather than a constrained direct-action object.

The direct path is best for compact, low-risk, inspectable actions whose semantics are already backed by an existing AgentC helper.

## Validation Matrix

| Test | Launch mode | Verifies |
|---|---|---|
| `EdictAgentcModuleTest.DirectEdictActionReadsFileThroughCuratedLauncher` | `edict.sh -` with mock runtime | Model-style direct action reads a file through curated launcher; result content/bytes match the equivalent `agentc_file_read!` helper envelope. |
| `EdictAgentcModuleTest.DirectEdictActionRejectsUnsafeShellOperation` | `edict.sh -` with mock runtime | Unsafe `shell` op returns `direct_action_denied` data envelope and does not execute the shell helper. |
| `EdictAgentcModuleTest.*` | cpp-agent test binary | Existing AgentC wrapper/provider/tool/stream behavior remains compatible with launcher prelude imports. |
| `EdictLlmModuleTest.*` | cpp-agent test binary | LLM provider/bootstrap/repl behavior remains compatible with launcher prelude imports. |

## Related Goals

- Parent: G078 — Edict-Resident Agent Loop Consolidation.
- Complements: G100 — Edict Isolation Contract Hardening.
- Depends on: G079 tool helper surface and curated launcher/provider module loading.

## Deferred Work

- Add more direct-action operations only behind explicit whitelist tests.
- Move broader isolation policy, native capability exclusion, and arbitrary emitted-source sandboxing into G100.
- Add richer direct-action schema validation if G094/G095 introduce native cognitive libraries that need compact direct invocation.
