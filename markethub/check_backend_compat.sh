#!/usr/bin/env bash
# DeltaGUI backend-contract compatibility checks for the AgentC MarketHub backend.
#
# Mirrors the same fixture/local routes exercised by DeltaGUI's
# check_backend_cpp.sh and check_backend_tradestation.sh:
#   /api/health, /api/config, /api/auth/{provider}/status, /api/ready,
#   /api/market/options?fixture=true, /api/quotes/status, /api/quotes/cache,
#   /api/stream/status, /api/stream/start, /api/stream/deltas,
#   /api/stream/fixture/update, /api/stream/options (501), /api/stream/stop,
#   404 handling.
#
# Live broker calls remain covered by live_backend_smoke.sh and opt-in
# MARKETHUB_LIVE_TESTS CTest entries — this script is credential-free.

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
BUILD_DIR=${MARKETHUB_BUILD_DIR:-"${REPO_ROOT}/build"}
BACKEND="${BUILD_DIR}/markethub/markethub_compat_backend"
BUILD=1
PROVIDERS=()

usage() {
    cat <<'USAGE'
Usage: markethub/check_backend_compat.sh [options]

Options:
  --provider NAME       Provider to check: schwab, tradestation, or both.
  --schwab              Shortcut for --provider schwab.
  --tradestation        Shortcut for --provider tradestation.
  --all                 Check both providers. Default.
  --build-dir DIR       CMake build directory. Default: MARKETHUB_BUILD_DIR or ./build.
  --no-build            Do not build markethub_compat_backend first.
  -h, --help            Show this help.
USAGE
}

add_provider() {
    case "$1" in
        schwab|tradestation) PROVIDERS+=("$1") ;;
        both) PROVIDERS=(schwab tradestation) ;;
        *) echo "error: unknown provider '$1'" >&2; exit 2 ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --provider)
            [[ $# -ge 2 ]] || { echo "error: --provider requires a value" >&2; exit 2; }
            add_provider "$2"
            shift 2
            ;;
        --schwab)
            add_provider schwab
            shift
            ;;
        --tradestation)
            add_provider tradestation
            shift
            ;;
        --all)
            add_provider both
            shift
            ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "error: --build-dir requires a value" >&2; exit 2; }
            BUILD_DIR=$2
            BACKEND="${BUILD_DIR}/markethub/markethub_compat_backend"
            shift 2
            ;;
        --no-build)
            BUILD=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown argument '$1'" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ${#PROVIDERS[@]} -eq 0 ]]; then
    PROVIDERS=(schwab tradestation)
fi

if [[ "$BUILD" -eq 1 ]]; then
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR" >/dev/null 2>&1 || true
    cmake --build "$BUILD_DIR" --target markethub_compat_backend -j "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
fi

if [[ ! -x "$BACKEND" ]]; then
    echo "error: AgentC compatibility backend not found: $BACKEND" >&2
    exit 2
fi

pids=()
logs=()
cleanup() {
    for pid in "${pids[@]:-}"; do
        kill "$pid" >/dev/null 2>&1 || true
        sleep 0.05
        kill -9 "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    done
    for log in "${logs[@]:-}"; do
        rm -f "$log"
    done
}
trap cleanup EXIT

run_provider() {
    local provider=$1
    local port
    case "$provider" in
        schwab) port=${MARKETHUB_COMPAT_SCHWAB_PORT:-18980} ;;
        tradestation) port=${MARKETHUB_COMPAT_TRADESTATION_PORT:-18981} ;;
        *) echo "error: unknown provider $provider" >&2; exit 2 ;;
    esac

    local log
    log=$(mktemp)
    logs+=("$log")
    "$BACKEND" --provider "$provider" --port "$port" >"$log" 2>&1 &
    local pid=$!
    pids+=("$pid")

    python3 - "$provider" "$port" <<'PY'
import json
import sys
import time
import urllib.error
import urllib.request

provider, port = sys.argv[1], int(sys.argv[2])
base = f"http://127.0.0.1:{port}"
backend_name = "agentc_markethub_backend"
provider_name = f"greekscope_backend_{provider}_rest"


def get(path):
    with urllib.request.urlopen(base + path, timeout=5) as resp:
        return json.loads(resp.read().decode("utf-8"))


def post(path, payload=None):
    body = b"" if payload is None else json.dumps(payload).encode("utf-8")
    headers = {}
    if payload is not None:
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(base + path, data=body, headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=5) as resp:
        return json.loads(resp.read().decode("utf-8"))


def expect_http(path, code):
    try:
        urllib.request.urlopen(base + path, timeout=5)
        raise AssertionError(f"expected HTTP {code} for {path}")
    except urllib.error.HTTPError as exc:
        assert exc.code == code, (path, exc.code)
        return json.loads(exc.read().decode("utf-8"))

# Wait for health
for _ in range(50):
    try:
        health = get("/api/health")
        if health.get("ok") is True:
            break
    except Exception:
        time.sleep(0.1)
else:
    raise AssertionError("backend did not become healthy")

assert health["backend"] == backend_name, health
assert health["provider"] == provider, health
print(f"PASS: {provider} health endpoint")

config = get("/api/config")
assert config["backend"] == backend_name, config
assert "config_present" in config, config
assert "client_secret_present" in config, config
assert "client_id_present" in config, config
assert "redirect_uri_present" in config, config
assert "default_symbol" in config, config
assert "strike_count" in config, config
assert "access_token" not in json.dumps(config).lower(), config
assert "refresh_token" not in json.dumps(config).lower(), config
print(f"PASS: {provider} config endpoint")

# /api/config/status alias must return the same shape
config_status = get("/api/config/status")
assert config_status["backend"] == backend_name, config_status
assert "config_present" in config_status, config_status
assert "client_secret_present" in config_status, config_status
print(f"PASS: {provider} config/status alias endpoint")

auth = get(f"/api/auth/{provider}/status")
assert auth["backend"] == backend_name, auth
assert "authenticated" in auth, auth
assert "token_file_present" in auth, auth
assert "configured" in auth, auth
assert "refresh_present" in auth, auth
assert "expires_at_unix" in auth, auth
assert "access_token" not in json.dumps(auth).lower(), auth
print(f"PASS: {provider} auth status endpoint")

try:
    ready = get("/api/ready")
except urllib.error.HTTPError as exc:
    assert exc.code == 503, exc.code
    ready = json.loads(exc.read().decode("utf-8"))
assert ready["config"]["backend"] == backend_name, ready
assert "capabilities" in ready, ready
assert "access_token" not in json.dumps(ready).lower(), ready
assert "refresh_token" not in json.dumps(ready).lower(), ready
print(f"PASS: {provider} readiness endpoint")

fixture = get("/api/market/options?symbol=AMD&fixture=true")
assert fixture["schema_version"] == 1, fixture
assert fixture["provider"] == provider_name, fixture
assert fixture["underlying"] == "AMD", fixture
assert fixture["metadata"]["backend_cache"]["cache"] == "bypass", fixture
assert len(fixture["contracts"]) == 2, fixture
required_fields = [
    "symbol", "underlying", "expiration", "days_to_expiration", "strike",
    "option_type", "multiplier", "bid", "ask", "last", "mark", "model_price",
    "model_residual", "implied_volatility", "open_interest", "volume", "timestamp",
]
for idx, contract in enumerate(fixture["contracts"]):
    for field in required_fields:
        assert field in contract, (idx, field, contract)
    assert contract["option_type"] in ("call", "put"), contract
    assert float(contract["strike"]) > 0, contract
    assert float(contract["days_to_expiration"]) > 0, contract
    assert float(contract["bid"]) >= 0, contract
    assert float(contract["ask"]) >= 0, contract
    assert float(contract["mark"]) >= 0, contract
    assert float(contract["implied_volatility"]) > 0, contract
print(f"PASS: {provider} fixture snapshot endpoint and contract shape")

quotes_status = get("/api/quotes/status?symbol=AMD")
assert quotes_status["ok"] is True, quotes_status
assert quotes_status["ready"] is True, quotes_status
assert quotes_status["known_contract_count"] >= 2, quotes_status
assert "sequence" in quotes_status, quotes_status
assert "symbol" in quotes_status, quotes_status
assert "stale" in quotes_status, quotes_status
assert "partial" in quotes_status, quotes_status
print(f"PASS: {provider} quote cache status endpoint")

cache = get("/api/quotes/cache?since=0")
assert cache["ok"] is True, cache
assert "sequence" in cache, cache
assert len(cache["quotes"]) >= 2, cache
for q in cache["quotes"]:
    assert "symbol" in q, q
    assert "bid" in q, q
    assert "ask" in q, q
    assert "last" in q, q
    assert "mark" in q, q
    assert "implied_volatility" in q, q
    assert "open_interest" in q, q
    assert "volume" in q, q
    assert "sequence" in q, q
    assert "timestamp" in q, q
print(f"PASS: {provider} quote cache bulk endpoint")

stream = get("/api/stream/status")
assert stream["backend"] == backend_name, stream
assert stream["state"] == "stopped", stream
print(f"PASS: {provider} stream status endpoint")

# Start streaming to populate fixture events
post("/api/stream/start?symbol=AMD&fixture=true")

fixture_deltas = get("/api/stream/deltas?symbol=AMD&fixture=true&since=0")
assert fixture_deltas["ok"] is True, fixture_deltas
assert len(fixture_deltas["events"]) >= 2, fixture_deltas
assert fixture_deltas["events"][0]["type"] in ("option_quotes", "underlying_quote"), fixture_deltas["events"][0]
assert "data" in fixture_deltas["events"][0], fixture_deltas["events"][0]
assert "stream" in fixture_deltas["events"][0], fixture_deltas["events"][0]
print(f"PASS: {provider} fixture delta endpoint")

# Regenerate fixture events
regen = post("/api/stream/fixture/generate")
assert regen["ok"] is True, regen
regen_deltas = get("/api/stream/deltas?since=0")
assert regen_deltas["ok"] is True, regen_deltas
assert len(regen_deltas["events"]) >= 2, regen_deltas
print(f"PASS: {provider} fixture generate endpoint")

post("/api/stream/fixture/update?symbol=AMD&bid=4.5&ask=4.7")
deltas = get("/api/stream/deltas?since=0")
assert deltas["ok"] is True, deltas
assert len(deltas["events"]) >= 1, deltas
last_ev = deltas["events"][-1]
assert last_ev["symbol"] == "AMD", last_ev
assert last_ev["data"]["bid"] == 4.5, last_ev
print(f"PASS: {provider} live delta endpoint")

placeholder = expect_http("/api/stream/options?symbol=AMD", 501)
assert placeholder["event_schema"]["type"] == "option_quote_delta", placeholder
print(f"PASS: {provider} stream placeholder endpoint")

live_start = post("/api/stream/start?symbol=AMD")
assert live_start["state"] == "starting", live_start
assert live_start.get("last_error", "") == "", live_start
if provider == "tradestation":
    assert "upstreams" in live_start, live_start
    assert "underlying" in live_start["upstreams"], live_start
    assert "options" in live_start["upstreams"], live_start
    assert live_start["upstreams"]["underlying"]["path"] == "/v3/marketdata/stream/quotes/AMD", live_start
    assert live_start["upstreams"]["options"]["path"] == "/v3/marketdata/stream/options/chains/AMD", live_start
    assert live_start["upstreams"]["options"]["strike_scope"] == "underlying_price_percent_band", live_start
    assert live_start["upstreams"]["options"]["strike_percent_band"] == 15.0, live_start
assert "access_token" not in json.dumps(live_start).lower(), live_start
assert "bearer" not in json.dumps(live_start).lower(), live_start
live_deltas = get("/api/stream/deltas?since=0")
assert live_deltas["ok"] is True, live_deltas
assert live_deltas["count"] == 0, live_deltas
print(f"PASS: {provider} live stream start compatibility")

stop = post("/api/stream/stop")
assert stop["state"] == "stopped", stop
print(f"PASS: {provider} stream stop endpoint")

not_found = expect_http("/api/not-implemented", 404)
assert not_found["ok"] is False, not_found
assert not_found["code"] == "not_found", not_found
print(f"PASS: {provider} 404 endpoint")
PY
}

for provider in "${PROVIDERS[@]}"; do
    run_provider "$provider"
done

if [[ " ${PROVIDERS[*]} " == *" schwab "* && " ${PROVIDERS[*]} " == *" tradestation "* ]]; then
    echo "PASS: Schwab/TradeStation compatibility providers both exercised"
fi

echo "All AgentC/DeltaGUI backend compatibility checks passed."
