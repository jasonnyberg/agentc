# Goal: G079 — Edict Agent Loop Tool Support

**Status**: COMPLETE  
**Created**: 2026-05-10  
**Reassessed**: 2026-05-11  
**Completed**: 2026-05-11

## Objective
Add a minimal, useful first tool surface to the Edict-owned agent loop so provider-driven chat sessions can read files, write/edit files, and invoke shell/system commands through explicit Edict/FFI capabilities rather than host-owned special cases.

## Rationale
The new `llm.init(...)`, stable provider objects, curated launcher, and `provider.repl()` loop have established the first viable Edict-resident UX seam. The next capability gap is tool use: without a small but real file/system surface, the loop can chat but cannot yet act as an engineering agent. This work should keep the control plane in Edict while exposing only narrow mechanical capabilities through FFI.

## Acceptance Criteria
- [x] Expose minimal file-reading capability to Edict/FFI suitable for agent-loop use.
- [x] Expose minimal file-writing and/or edit capability to Edict/FFI with a narrow, explicit contract.
- [x] Expose minimal shell/system-command execution to Edict/FFI.
- [x] Define Edict-side wrapper words and calling patterns so tools are usable directly from provider-driven loops.
- [x] Demonstrate the first tool-aware provider-loop flow under the curated launcher.

## Initial Scope
- File read
- File write/edit
- Shell/system command execution

## Constraints
- Keep capability boundaries explicit and narrow.
- Prefer Edict-side orchestration and policy over host-owned special cases.
- Preserve the curated launcher / provider-object architecture rather than bypassing it.

## Reassessment — 2026-05-11
This is now the immediate next implementation slice. The provider loop is usable for chat, and Codex/local providers can be initialized through the Edict launcher, but the loop cannot yet act as a coding agent without explicit file and shell capabilities. Keep the first version narrow, policy-visible, and Edict-orchestrated.

## Implementation Notes — 2026-05-11
- Added native extension functions in `extensions/agentc_stdlib.{h,cpp}`:
  - `agentc_ext_file_read_json_cstr(path, max_bytes)`
  - `agentc_ext_file_write_json_cstr(path, content)`
  - `agentc_ext_file_replace_json_cstr(path, old_text, new_text)`
  - `agentc_ext_shell_exec_json_cstr(command, max_bytes)`
- Each function returns a JSON envelope with an explicit `ok` list sentinel, operation metadata, result fields, and structured error object on failure.
- Added Edict wrappers in `cpp-agent/edict/modules/agentc.edict`:
  - `agentc_file_read !`
  - `agentc_file_write !`
  - `agentc_file_replace !`
  - `agentc_shell !`
  - `agentc_tools.{read_file,write_file,replace_file,shell}`
- Added `provider.tools = agentc_tools` in `llm.edict` so provider-scoped loops can invoke tools in the provider context where imported `ext` bindings are available.

Provider-scoped usage pattern:

```edict
llm.init([local-qwen]) @provider
provider < [/tmp/note.txt] [hello] tools.write_file ! @last_tool > pop /
provider < [/tmp/note.txt] tools.read_file ! @last_tool > pop /
provider.last_tool.content print
```

## Validation — 2026-05-11
- `cmake --build build --target agentc_extensions cpp_agent_tests -j2`
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictAgentcModuleTest.ToolWrappersReadWriteReplaceAndShell:EdictAgentcModuleTest.ProviderCarriesToolSurfaceUnderCuratedLauncher'` — passed 2/2.
- `./build/cpp-agent/cpp_agent_tests --gtest_filter='EdictLlmModuleTest.*:EdictAgentcModuleTest.ToolWrappersReadWriteReplaceAndShell:EdictAgentcModuleTest.ProviderCarriesToolSurfaceUnderCuratedLauncher:EdictAgentRootTest.*:AgentRootVmOpsTest.*'` — passed 17/17.
- `./build/edict/edict_tests --gtest_filter='EdictVM.*'` — passed 22/22.

Known unrelated baseline: `EdictAgentcModuleTest.StreamWrapperSpawnsAndSynchronizes` still fails because the deferred G074 stream wrapper/runtime JSON surface is incomplete.

## Relationship To G078
G078 establishes the Edict-resident loop and provider surface. G079 extends that loop with its first action/tool capabilities.
