---
name: agentc
description: Provides high-performance, persistent logical and transactional cognitive state management using the AgentC Edict VM.
---
# Skill: AgentC Cognitive Substrate

## Description
Provides high-performance, persistent logical and transactional cognitive state management using the AgentC Edict VM over a Unix Domain Socket interface.

## Available Tools
- `agentc_eval`: Executes raw Edict bytecode in the persistent VM session. Use this for general stack manipulation, math, and defining Edict functions.
- `agentc_import_ffi`: Imports compiled C/C++ shared libraries into the VM.
- `agentc_logic_query`: Executes a Mini-Kanren logic query against the substrate.

## Workflows

### 1. Initializing the Logic Engine
Before running a logic query, you **must** import the logic FFI using `agentc_import_ffi`.
*   `library_path`: Absolute path to `libkanren.so` (e.g. `/path/to/build/kanren/libkanren.so`)
*   `header_path`: Absolute path to `kanren_runtime_ffi_poc.h` (e.g. `/path/to/cartographer/tests/kanren_runtime_ffi_poc.h`)
*   `module_alias`: `logicffi`
*   `bindings`: `{ "agentc_logic_eval_ltv": "logic" }`

### 2. Querying Logic
Once imported, use `agentc_logic_query` and provide the JSON object spec.
Example Query Spec:
```json
{
  "fresh": ["q"],
  "where": [["==", "q", "tea"]],
  "results": ["q"]
}
```

## Error Handling
If you encounter `Stale schema` errors or `Failed to map header`, ensure your file paths provided to the FFI tool are absolute and that the libraries have been correctly built.
