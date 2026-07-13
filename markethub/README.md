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
- `include/markethub/brokerage_provider.h` — provider-owned brokerage auth/API
  request boundary for TradeStation and Schwab. Builds OAuth URLs, token
  exchange/refresh POST specs, and market-data GET specs without executing live
  HTTP or moving credentials/tokens into Edict/worker state. TradeStation is the
  preferred provider path; Schwab remains supported behind the same interface.
- `markethub_live_smoke` — optional libcurl-linked operator harness for Phase 4
  live-provider testing. It reads local `tradestation.local.json` /
  `schwab.local.json` plus token files from `AGENTC_MARKETHUB_CONFIG_DIR`,
  `GREEKSCOPE_CONFIG_DIR`, or `~/GreekScope/config`; injects bearer tokens only
  inside the native process; executes one request spec; and publishes a minimal
  live underlying quote as a canonical hub event. It never prints tokens or
  client secrets.
- `src/` — implementations.
- `facade/greekscope_facade.cpp` — AgentC-owned `extern "C"` façade over the
  DeltaGUI/GreekScope GEX analytics capability (Phase 1 transitional bridge).
  Compiled directly from `~/DeltaGUI/backend_cpp` sources without modifying them;
  built only when that tree is present (`AGENTC_MARKETHUB_DELTAGUI_ROOT`).
- `tests/` — `markethub_tests` (hub MVP + highlights + TCC bridge demo).

## Live smoke harness

After OAuth has created local provider config/token files, run for example:

```bash
./markethub/live_backend_smoke.sh --all --symbol AMD --mode underlying

# Or call the native harness directly:
./build/markethub/markethub_live_smoke --provider tradestation --symbol AMD --mode underlying
./build/markethub/markethub_live_smoke --provider schwab --symbol AMD --mode underlying
```

Use `live_backend_smoke.sh --mode auth-url` to print provider authorization URLs
without exposing client secrets, and `--mode token-status` to check local token
freshness without making provider market-data calls. The default config directory
is `AGENTC_MARKETHUB_CONFIG_DIR`, then `GREEKSCOPE_CONFIG_DIR`, then
`~/GreekScope/config`.

Expected local file shape:

```jsonc
// <config-dir>/tradestation.local.json or <config-dir>/schwab.local.json
{
  "client_id": "[REDACTED]",
  "client_secret": "[REDACTED]",
  "redirect_uri": "http://localhost:8080/callback",
  "symbol": "AMD",
  "strike_count": 12
}
```

```jsonc
// <config-dir>/tradestation_tokens.local.json or <config-dir>/schwab_tokens.local.json
{
  "access_token": "[REDACTED]",
  "refresh_token": "[REDACTED]",
  "expires_at_unix": 4102444800,
  "refresh_expires_at_unix": 4102444800
}
```

Keep those files outside git (or under an ignored local path) and preferably
`chmod 600` them before live testing.

### Opt-in automated live tests

Live broker tests are registered with CTest but are skipped unless explicitly
enabled. After creating the local config/token files above, run:

```bash
cmake --build build --target markethub_live_smoke -j2

MARKETHUB_LIVE_TESTS=1 \
AGENTC_MARKETHUB_CONFIG_DIR="$HOME/GreekScope/config" \
ctest --test-dir build -L live --output-on-failure
```

To run only TradeStation:

```bash
MARKETHUB_LIVE_TESTS=1 \
AGENTC_MARKETHUB_CONFIG_DIR="$HOME/GreekScope/config" \
ctest --test-dir build -R 'markethub_live_tradestation' --output-on-failure
```

The live CTest entries use `live_backend_smoke.sh --require-live`; without
`MARKETHUB_LIVE_TESTS=1`, or when the provider's local credential/token files
are absent, they return CTest skip code 77 instead of failing the normal unit
suite. Optional environment knobs: `MARKETHUB_SYMBOL`, `MARKETHUB_STRIKE_COUNT`,
and `MARKETHUB_BUILD_DIR`.

## DeltaGUI backend-contract compatibility

`markethub_compat_backend` is a standalone HTTP server that mirrors the
DeltaGUI `greekscope_backend_cpp` fixture/local route contract — the same
endpoints exercised by DeltaGUI's `check_backend_cpp.sh` and
`check_backend_tradestation.sh`:

| Route | Behavior |
|-------|----------|
| `GET /api/health` | `{ok, backend, provider, time}` |
| `GET /api/config` | `{backend, config_present, client_secret_present, ...}` |
| `GET /api/auth/{provider}/status` | `{backend, authenticated, token_file_present}` |
| `GET /api/ready` | 503 + `{config, capabilities}` when not configured |
| `GET /api/market/options?symbol=...&fixture=true` | MarketSnapshot with 17-field contracts |
| `GET /api/quotes/status?symbol=...` | `{ok, ready, known_contract_count}` |
| `GET /api/quotes/cache?since=N` | `{ok, quotes:[...]}` |
| `GET /api/stream/status` | `{backend, state, stale, last_sequence}` |
| `POST /api/stream/start?symbol=...[&fixture=true]` | Fixture populates events; live returns `starting` + upstreams |
| `GET /api/stream/deltas?since=N` | `{ok, latest_sequence, count, events:[...]}` |
| `POST /api/stream/fixture/update?symbol=...&bid=...&ask=...` | Injects a delta event |
| `GET /api/stream/options?symbol=...` | 501 placeholder with `event_schema` |
| `POST /api/stream/stop` | `{state: "stopped"}` |
| * | 404 `{ok:false, code:"not_found"}` |

Run the compatibility checks:

```bash
# Both providers (default):
./markethub/check_backend_compat.sh

# Single provider:
./markethub/check_backend_compat.sh --schwab
./markethub/check_backend_compat.sh --tradestation
```

Or via CTest:

```bash
ctest --test-dir build -L compat --output-on-failure
```

These tests are credential-free and run by default in the normal test suite.
They verify that the AgentC-based backend produces the same HTTP contract
shapes and field semantics as the native DeltaGUI backend.

## Design references

- `LocalContext/Knowledge/Goals/G117-DeltaGuiBackendToolAdapterExploration/index.md`
- `LocalContext/Knowledge/WorkProducts/WP_G117_MarketDataHubPhase0Inventory.md`
