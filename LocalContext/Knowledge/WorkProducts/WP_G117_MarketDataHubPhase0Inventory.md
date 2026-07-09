# WP — G117 Market Data Hub: Phase 0 Inventory, Classification, and Canonical Schemas

**Created**: 2026-07-07
**Goal**: 🔗[G117 — AgentC / DeltaGUI Market Data Hub Integration](../Goals/G117-DeltaGuiBackendToolAdapterExploration/index.md)
**Status**: Phase 0 design artifact

## 1. DeltaGUI/GreekScope Backend Capability Inventory

Evidence root: `/home/jwnyberg/DeltaGUI/backend_cpp` (read-only inspection; no DeltaGUI files modified).

| Capability | Evidence | Classification | Rationale |
|---|---|---|---|
| Provider config/auth (Schwab OAuth, TradeStation OAuth, tokens, readiness) | `src/providers/{schwab,tradestation}_provider.*`, `provider_factory.cpp`, `include/greekscope/provider.h` | **Provider boundary** (neither hub nor façade) | Credentials, OAuth state, and transport handles must remain behind native/provider adapters per G117 safety constraints. Never crosses into Edict/worker state. |
| Upstream stream transports (Schwab WSS, TradeStation SSE) | `src/ws_client.cpp`, provider implementations | **Provider boundary** | Socket lifecycle is process-local, non-durable. |
| `QuoteCache` (thread-safe symbol→Quote map, sequence-ordered) | `include/greekscope/quote_cache.h`, `src/quote_cache.cpp` (32 lines) | **Hub responsibility** | Trivially rebuilt natively in AgentC hub module as the current-quote cache. |
| `StreamManager` / `StreamQuery` / `StreamEvent` (subscriptions, event accumulator, bounded history: `history_max_events_=5000`, `history_max_age_seconds_=900`, `stale_after_seconds_=30`, fixture generation) | `include/greekscope/stream_manager.h`, `stream_query.h` | **Hub responsibility** | This *is* the pub/sub + retention model the AgentC hub owns. Canonical event schemas below are derived from `StreamEvent`/`StreamDataType`. |
| GEX analytics (Black-Scholes gamma, GEX book, walls, flip point, snapshot JSON) | `src/gex/*.cpp` + `src/analytics_service.cpp` (~800 lines), headers under `include/greekscope/gex/` | **Source** (Phase 1 façade target) | Safe/read-only, fixture-seedable (`seed_from_snapshot_json` → `gex_snapshot_json`), pure JSON/string contracts, no credentials/sockets/curl includes, self-contained C++. Ideal anti-corruption-boundary capability. |
| HTTP server + REST routing (`/api/health`, `/api/market/options`, `/api/stream/*`, `/api/analytics/gex`, `/api/quotes/*`) | `src/main.cpp` | **Subscriber responsibility** | DeltaGUI-facing presentation/transport; stays in DeltaGUI. The hub's DeltaGUI-shaped subscriber contract mirrors `/api/stream/deltas` semantics (since-sequence replay). |
| Fixture data generation | `StreamManager::generate_fixture_events`, `fixture_deltas_json` | **Hub responsibility** | Hub MVP re-implements deterministic fixture ingest for provider-free validation. |

## 2. Canonical Event Schemas

All hub events share one envelope shape (`MarketEvent`), discriminated by `type`. Flat numeric/text field maps keep the schema additive and JSON/Listree-friendly.

### Common envelope fields

| Field | Type | Semantics |
|---|---|---|
| `type` | string | `underlying_quote` \| `option_quote` \| `option_chain_snapshot` \| `history_sample` \| `highlight` |
| `symbol` | string | Underlying symbol (uppercase) |
| `contract` | string | Option contract symbol; empty for underlying-level events |
| `source` | string | `fixture` \| provider name \| `facade:<name>` \| `highlight:<rule_id>` |
| `sequence` | int64 | Hub-stamped, strictly monotonic per bus |
| `observed_unix` | int64 | Provider/exchange observation time (0 = unknown) |
| `received_unix` | int64 | Hub ingest time |
| `num.*` | map<string,double> | Numeric payload fields |
| `text.*` | map<string,string> | String payload fields |

### Per-type payload conventions

- **`underlying_quote`** — `num`: `bid`, `ask`, `last`, `mark`, `volume`. Freshness metadata via `observed_unix`.
- **`option_quote`** — `num`: `bid`, `ask`, `last`, `mark`, `implied_volatility`, `open_interest`, `volume`, plus Greeks when available (`delta`, `gamma`, `theta`, `vega`).
- **`option_chain_snapshot`** — bounded marker event: `num`: `contract_count`, `atm_strike`, `spot`; constituent contracts are emitted as individual `option_quote` events sharing the marker's `observed_unix`. This keeps the envelope flat while remaining bounded.
- **`history_sample`** — `num`: `value`, `bucket_unix`, `source_sequence`; `text`: `field`. Produced by history queries (downsampled series), not re-published on the bus in the MVP (prevents feedback loops).
- **`highlight`** — `text`: `rule_id`, `kind` (`price_move` \| `stale_quote`), `detail`; `num`: rule-specific (`change_pct`, `from`, `to`, `window_seconds` for price moves; `age_seconds` for staleness). Highlights are re-published onto the same bus so all subscriber shapes consume one uniform stream.

## 3. Subscriber Query/Envelope Semantics

Two first-class subscriber shapes (mirroring the G117 acceptance criteria):

1. **DeltaGUI-shaped delta feed** (`DeltaFeedSubscriber`): filtered subscription with bounded replay buffer; query `deltas_since(sequence)` returns `{"ok":true,"latest_sequence":N,"count":K,"events":[...]}` — the same since-sequence contract as `/api/stream/deltas`.
2. **AgentC/Edict-shaped envelope collector** (`EnvelopeCollectorSubscriber`): accumulates events; `collect()` drains and returns `{"ok":true,"status":"collected","count":K,"events":[...]}` — matching AgentC worker/TCC envelope conventions so Edict agents consume hub data like any worker result.

Subscription filters are declarative: event-type list (empty = all) + symbol list (empty = all). Highlight subscribers receive derived events without raw-quote fan-in.

## 4. History Retention / Resolution / Freshness Policy

- **Policy** (`HistoryPolicy`): `resolution_seconds` (bucket width; last-write-wins within a bucket) + `max_samples` per series (oldest dropped first). Defaults: 60s resolution, 1000 samples. Per-hub configurable; per-subscriber policies are a later phase.
- **Series key**: `symbol|contract|field` — every numeric field of ingested quote events becomes a downsampled series.
- **Freshness rule**: an entity is *stale* when `now_unix - latest.received_unix > freshness_seconds` (default 30s, from DeltaGUI's `stale_after_seconds_`). Staleness is evaluated with an injected clock so rules are machine-testable.
- **Data quality**: missing numeric fields are absent (never fabricated as 0 by the hub); `observed_unix=0` means provider time unknown and freshness falls back to `received_unix`.

## 5. Ownership Boundaries

| Asset | Owner | Rule |
|---|---|---|
| OAuth credentials, tokens, client secrets | Provider adapters (native) | Never enter Edict dictionaries, worker envelopes, or hub events |
| Sockets/WSS/SSE handles | Provider adapters | Process-local, non-durable |
| C++ objects (`AnalyticsService`, `GexBook`) | Façade library (process-local singletons) | Only JSON/string/scalar contracts cross the C ABI |
| Façade-returned strings | Caller | Explicit ownership: every `const char*` returned by the façade is malloc-owned and released via `agentc_greekscope_free` |
| Subscriber policy, event semantics, history | AgentC hub module | Providers own transport only |
| Hub module code | `markethub/` (separate module) | Consumes AgentC facilities (TCC allowlist/workers, envelope conventions); never linked into or referenced by core AgentC machinery |

## 6. Phase 1 Façade Selection

**Selected capability**: GEX analytics (`AnalyticsService::seed_from_snapshot_json` + `gex_snapshot_json`).

**Why**: read-only and side-effect-free beyond process-local state; fixture-seedable (no live provider or auth needed); pure JSON-in/JSON-out; self-contained sources (`src/gex/*` + `analytics_service.cpp` include only std + own headers + `json.hpp`); high product value (gamma walls/flip points are exactly the derived "highlights" the hub publishes).

**Shape**: AgentC-side adapter library `agentc_greekscope_facade` compiled *in the AgentC build* directly from the DeltaGUI sources (path via `AGENTC_DELTAGUI_BACKEND_ROOT`, auto-probed at `~/DeltaGUI/backend_cpp`); zero DeltaGUI modifications. C ABI:

```c
const char* agentc_greekscope_facade_version(void);
const char* agentc_greekscope_gex_seed_snapshot(const char* snapshot_json);
const char* agentc_greekscope_gex_snapshot(const char* symbol);
int         agentc_greekscope_gex_has_symbol(const char* symbol);
void        agentc_greekscope_free(const char* text);
```

Registered through the G116 symbol allowlist and invoked from isolated TCC workers; results feed the hub as `source="facade:greekscope_gex"` events.
