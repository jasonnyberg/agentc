# Work Product: `cpp-agent` Runtime File Structure Refactor

## Purpose
Define the target source layout for keeping the existing `cpp-agent/` directory while reorganizing it around:
- a reusable common runtime core,
- provider-specific implementation libraries,
- a single stable Edict-facing C ABI,
- thin host/front-end executables.

## Decision Summary
Keep `cpp-agent/` as the top-level integration directory, but reorganize its contents so that:
1. **Common runtime code** is isolated from host/UI code.
2. **Provider-specific code** lives in separate provider subtrees/libraries.
3. **Only one simple C ABI** is exposed to Edict via FFI import.
4. Host executables become thin shells over the runtime + embedded Edict VM.
5. Persistent logical state continues to live in the existing **mmap-backed Listree/slab** substrate rather than inside provider/runtime libraries.

## Target Build Outputs

### Public Edict-facing shared library
- `libagent_runtime.so`
  - stable C ABI defined in 🔗[AgentcRuntimeCAbi](../Contracts/AgentcRuntimeCAbi.md)
  - imported by Edict / Cartographer

### Internal common runtime library
- `agent_runtime_core`
  - config parsing
  - provider registry
  - request normalization
  - response normalization
  - credentials and auth helpers
  - HTTP helpers
  - error/trace generation

### Internal provider libraries
- `agent_provider_google`
- `agent_provider_openai`
- `agent_provider_copilot`

These are not directly imported by Edict. They are selected behind the runtime boundary.

### Thin host/front-end executables
- `cpp-agent-shell`
- `cpp-agent-socket`
- optional later: `cpp-agent-batch`

These embed Edict, import/use the runtime library, provide transport/lifecycle behavior, and restore/persist the mmap-backed Listree VM state.

## Proposed Directory Layout

```text
cpp-agent/
├── CMakeLists.txt
├── include/
│   └── agentc_runtime/
│       └── agentc_runtime.h        # stable public C ABI header
│
├── runtime/
│   ├── common/
│   │   ├── credentials.cpp
│   │   ├── credentials.h
│   │   ├── http_client.cpp
│   │   ├── http_client.h
│   │   ├── sse_parser.cpp
│   │   ├── json_util.cpp
│   │   ├── json_util.h
│   │   ├── error.cpp
│   │   └── error.h
│   │
│   ├── core/
│   │   ├── runtime.cpp
│   │   ├── runtime.h
│   │   ├── runtime_state.h
│   │   ├── config.cpp
│   │   ├── config.h
│   │   ├── request.cpp
│   │   ├── request.h
│   │   ├── response.cpp
│   │   ├── response.h
│   │   ├── provider_registry.cpp
│   │   ├── provider_registry.h
│   │   ├── provider_interface.h
│   │   ├── trace.cpp
│   │   └── trace.h
│   │
│   ├── providers/
│   │   ├── google/
│   │   │   ├── google_provider.cpp
│   │   │   └── google_provider.h
│   │   ├── openai/
│   │   │   ├── openai_provider.cpp
│   │   │   └── openai_provider.h
│   │   └── copilot/
│   │       ├── copilot_provider.cpp
│   │       └── copilot_provider.h
│   │
│   └── c_api/
│       └── agentc_runtime_c_api.cpp
│
├── edict/
│   ├── modules/
│   │   └── agentc.edict            # high-level Edict wrapper namespace
│   └── bridge/
│       ├── agentc_runtime_bridge.cpp   # optional helper conversions if needed
│       └── agentc_runtime_bridge.h
│
├── hosts/
│   ├── common/
│   │   ├── host_session.cpp
│   │   ├── host_session.h
│   │   ├── host_commands.cpp
│   │   └── host_commands.h
│   ├── shell/
│   │   └── main.cpp
│   ├── socket/
│   │   ├── main.cpp
│   │   ├── socket_server.cpp
│   │   └── socket_server.h
│   └── batch/
│       └── main.cpp
│
├── demos/
│   ├── demo_gemini.sh
│   └── hello_gemini.cpp            # transitional smoke test until migrated/replaced
│
└── tests/
    ├── runtime/
    │   ├── config_contract_test.cpp
    │   ├── response_normalization_test.cpp
    │   ├── runtime_abi_test.cpp
    │   └── provider_selection_test.cpp
    ├── providers/
    │   ├── google_provider_test.cpp
    │   ├── openai_provider_test.cpp
    │   └── copilot_provider_test.cpp
    └── hosts/
        ├── socket_host_test.cpp
        └── shell_host_smoke_test.cpp
```

## Mapping From Current Files

### Move into `runtime/common/`
Current files:
- `cpp-agent/credentials.cpp`
- `cpp-agent/credentials.h`
- `cpp-agent/http_client.cpp`
- `cpp-agent/http_client.h`
- `cpp-agent/sse_parser.cpp`

### Move into `runtime/providers/`
Current files:
- `cpp-agent/providers/google.cpp`
- `cpp-agent/providers/google.h`
- `cpp-agent/providers/openai.cpp`
- `cpp-agent/providers/openai.h`

### Reduce / retire from canonical path
Current files:
- `cpp-agent/agent_loop.cpp`
- `cpp-agent/agent_loop.h`
- `cpp-agent/agent_types.h`
- `cpp-agent/ai_types.h`
- `cpp-agent/api_registry.cpp`
- `cpp-agent/api_registry.h`
- `cpp-agent/edict_tools.cpp`
- `cpp-agent/edict_tools.h`

These were correct for the outer-C++-loop prototype, but should no longer own the long-term architecture.

### Move into `hosts/`
Current files:
- `cpp-agent/main.cpp`
- `cpp-agent/socket_server.cpp`
- `cpp-agent/socket_server.h`

## Internal C++ Runtime Interface
The public surface to Edict is C ABI only, but internally provider implementations should share a C++ interface.

### Proposed provider interface
```cpp
struct RuntimeRequest;
struct RuntimeResponse;
struct RuntimeContext;

class ProviderInterface {
public:
    virtual ~ProviderInterface() = default;
    virtual std::string name() const = 0;
    virtual bool isEnabled(const RuntimeContext& ctx) const = 0;
    virtual RuntimeResponse invoke(const RuntimeRequest& request, RuntimeContext& ctx) = 0;
};
```

## Selection Flow
1. Host restores or initializes the mmap-backed Listree/Edict VM state.
2. Host or Edict creates/configures one runtime handle from JSON.
3. Runtime parses config into internal state.
4. `agentc_runtime_request_json(...)` parses request JSON.
5. Runtime resolves provider/model using config + request override precedence.
6. Registry selects the matching provider implementation.
7. Provider adapter performs transport/auth/vendor parsing.
8. Core normalizes the result into the contract in 🔗[AgentcRuntimeJsonContract](../Contracts/AgentcRuntimeJsonContract.md).
9. C ABI returns that JSON to Edict.
10. Edict updates durable logical state in the mmap-backed Listree substrate.

## Why This Layout

### Keeps `cpp-agent/`
No top-level rename is needed. Existing build/scripts can evolve incrementally.

### Separates reusable runtime from hosts
The runtime library becomes usable from:
- Edict-imported shared library
- shell host
- socket host
- future tests and demos

### Makes providers replaceable
Each provider implementation is isolated and testable.

### Preserves transition path
Existing files can be moved/refactored in slices without forcing a one-shot rewrite.

## Migration Sequence

### Slice 1
- Add `include/agentc_runtime/agentc_runtime.h`
- Add `runtime/core/` scaffolding
- Add `runtime/c_api/agentc_runtime_c_api.cpp`
- Keep current hosts working

### Slice 2
- Move common transport/auth code into `runtime/common/`
- Move provider files into `runtime/providers/`
- Introduce provider registry / internal interfaces

### Slice 3
- Add `edict/modules/agentc.edict`
- Implement Edict-side wrappers around imported ABI calls

### Slice 4
- Split current `main.cpp` into `hosts/shell/main.cpp` and `hosts/socket/main.cpp`
- Reduce legacy outer loop ownership

### Slice 5
- Mark legacy C++ outer-loop files transitional or archive them once Edict-native orchestration lands

## Recommendation
Adopt this layout as the implementation target after the runtime contracts. It preserves the working `cpp-agent/` integration area while reshaping it into a reusable runtime stack with a single simple Edict-facing ABI and provider-specific implementation libraries behind it.