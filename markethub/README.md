# AgentC Market Data Hub (`markethub/`)

Downstream application module for G117: an AgentC-hosted market-data pub/sub hub
for provider subscriptions, normalized stock/option events, bounded history,
derived highlights, and DeltaGUI/agent subscribers.

## Separation constraint (G117)

This module is intentionally **kept separate from the core AgentC machinery**.
It is a *consumer/application* of AgentC facilities:

- It links against `libedict` only from its **tests/bridge** side (to drive the
  G116 TCC symbol allowlist + isolated worker execution).
- The hub library itself (`markethub`) is standalone C++ (std-only) and is never
  linked into `libedict`, the Edict VM, or any core AgentC target.
- No new VM opcodes, no `edict_vm_*.cpp` additions, no core-header changes.
- Dependency direction is one-way: `markethub → AgentC`, never `AgentC → markethub`.

## Layout

- `include/markethub/market_event.h` — canonical event model (`underlying_quote`,
  `option_quote`, `option_chain_snapshot`, `history_sample`, `highlight`).
- `include/markethub/market_hub.h` — pub/sub hub: subscriptions, quote cache,
  bounded/downsampled history, highlight engine, fixture ingest.
- `src/` — implementations.
- `facade/greekscope_facade.cpp` — AgentC-owned `extern "C"` façade over the
  DeltaGUI/GreekScope GEX analytics capability (Phase 1 transitional bridge).
  Compiled directly from `~/DeltaGUI/backend_cpp` sources without modifying them;
  built only when that tree is present (`AGENTC_MARKETHUB_DELTAGUI_ROOT`).
- `tests/` — `markethub_tests` (hub MVP + highlights + TCC bridge demo).

## Design references

- `LocalContext/Knowledge/Goals/G117-DeltaGuiBackendToolAdapterExploration/index.md`
- `LocalContext/Knowledge/WorkProducts/WP_G117_MarketDataHubPhase0Inventory.md`
