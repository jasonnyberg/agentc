#!/usr/bin/env bash
# G117 live-provider smoke wrapper for markethub_live_smoke.
#
# This script intentionally does not accept secrets on the command line. Put
# provider config/token JSON files in AGENTC_MARKETHUB_CONFIG_DIR,
# GREEKSCOPE_CONFIG_DIR, or ~/GreekScope/config. The native harness injects
# bearer tokens inside the process and never prints them.

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
BUILD_DIR=${MARKETHUB_BUILD_DIR:-"${REPO_ROOT}/build"}
CONFIG_DIR=${AGENTC_MARKETHUB_CONFIG_DIR:-${GREEKSCOPE_CONFIG_DIR:-"${HOME:-.}/GreekScope/config"}}
MODE=${MARKETHUB_MODE:-underlying}
SYMBOL=${MARKETHUB_SYMBOL:-AMD}
STRIKE_COUNT=${MARKETHUB_STRIKE_COUNT:-12}
RAW=0
BUILD=1
REQUIRE_LIVE=0
SKIP_CODE=77
PROVIDERS=()

usage() {
    cat <<'USAGE'
Usage: markethub/live_backend_smoke.sh [options]

Builds the optional markethub_live_smoke harness, then calls TradeStation and/or
Schwab through the provider-owned native backend boundary.

Options:
  --provider NAME       Provider to call: tradestation, schwab, or both.
                        May be repeated. Default: both.
  --tradestation        Shortcut for --provider tradestation.
  --schwab              Shortcut for --provider schwab.
  --all                 Call both providers.
  --mode MODE           underlying, option-search, option-chain, auth-url, or
                        token-status. Default: MARKETHUB_MODE or underlying.
  --symbol SYMBOL       Symbol for market-data calls. Default: MARKETHUB_SYMBOL
                        or AMD.
  --strike-count N      Strike count for option modes. Default:
                        MARKETHUB_STRIKE_COUNT or 12.
  --config-dir DIR      Directory containing <provider>.local.json and
                        <provider>_tokens.local.json. Also exported as
                        AGENTC_MARKETHUB_CONFIG_DIR for the harness.
  --build-dir DIR       CMake build directory. Default: MARKETHUB_BUILD_DIR or
                        ../build from this script.
  --raw                 Forward --raw to the harness to print provider response
                        bodies for successful/failed HTTP calls.
  --no-build            Do not configure/build markethub_live_smoke first.
  --require-live        For automated live tests: exit 77 unless
                        MARKETHUB_LIVE_TESTS=1/true/yes/on and required local
                        provider credential/token files are readable.
  -h, --help            Show this help.

Credential files are local-only and should not be committed:
  <config-dir>/tradestation.local.json
  <config-dir>/tradestation_tokens.local.json
  <config-dir>/schwab.local.json
  <config-dir>/schwab_tokens.local.json

Example:
  AGENTC_MARKETHUB_CONFIG_DIR=$HOME/GreekScope/config \
    markethub/live_backend_smoke.sh --all --symbol AMD --mode underlying

For a credential-safe preflight without network calls:
  markethub/live_backend_smoke.sh --all --mode token-status
USAGE
}

add_provider() {
    case "$1" in
        tradestation|schwab)
            PROVIDERS+=("$1")
            ;;
        both)
            PROVIDERS=(tradestation schwab)
            ;;
        *)
            echo "error: unknown provider '$1' (expected tradestation, schwab, or both)" >&2
            exit 2
            ;;
    esac
}

live_tests_enabled() {
    case "${MARKETHUB_LIVE_TESTS:-}" in
        1|true|TRUE|yes|YES|on|ON) return 0 ;;
        *) return 1 ;;
    esac
}

provider_creds_available() {
    local provider=$1
    local config_file="${CONFIG_DIR}/${provider}.local.json"
    local token_file="${CONFIG_DIR}/${provider}_tokens.local.json"

    if [[ ! -r "$config_file" ]]; then
        echo "SKIP provider=${provider}: missing readable config file ${config_file}" >&2
        return 1
    fi

    if [[ "$MODE" != "auth-url" && ! -r "$token_file" ]]; then
        echo "SKIP provider=${provider}: missing readable token file ${token_file}" >&2
        return 1
    fi

    return 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --provider)
            [[ $# -ge 2 ]] || { echo "error: --provider requires a value" >&2; exit 2; }
            add_provider "$2"
            shift 2
            ;;
        --tradestation)
            add_provider tradestation
            shift
            ;;
        --schwab)
            add_provider schwab
            shift
            ;;
        --all)
            add_provider both
            shift
            ;;
        --mode)
            [[ $# -ge 2 ]] || { echo "error: --mode requires a value" >&2; exit 2; }
            MODE=$2
            shift 2
            ;;
        --symbol)
            [[ $# -ge 2 ]] || { echo "error: --symbol requires a value" >&2; exit 2; }
            SYMBOL=$2
            shift 2
            ;;
        --strike-count)
            [[ $# -ge 2 ]] || { echo "error: --strike-count requires a value" >&2; exit 2; }
            STRIKE_COUNT=$2
            shift 2
            ;;
        --config-dir)
            [[ $# -ge 2 ]] || { echo "error: --config-dir requires a value" >&2; exit 2; }
            CONFIG_DIR=$2
            shift 2
            ;;
        --build-dir)
            [[ $# -ge 2 ]] || { echo "error: --build-dir requires a value" >&2; exit 2; }
            BUILD_DIR=$2
            shift 2
            ;;
        --raw)
            RAW=1
            shift
            ;;
        --no-build)
            BUILD=0
            shift
            ;;
        --require-live)
            REQUIRE_LIVE=1
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
    PROVIDERS=(tradestation schwab)
fi

case "$MODE" in
    underlying|option-search|option-chain|auth-url|token-status) ;;
    *)
        echo "error: unsupported mode '$MODE'" >&2
        exit 2
        ;;
esac

export AGENTC_MARKETHUB_CONFIG_DIR="$CONFIG_DIR"

if [[ "$REQUIRE_LIVE" -eq 1 ]] && ! live_tests_enabled; then
    echo "SKIP: set MARKETHUB_LIVE_TESTS=1 to enable live broker tests" >&2
    exit "$SKIP_CODE"
fi

if [[ "$BUILD" -eq 1 ]]; then
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target markethub_live_smoke -j "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
fi

HARNESS="${BUILD_DIR}/markethub/markethub_live_smoke"
if [[ ! -x "$HARNESS" ]]; then
    echo "error: harness not found or not executable: $HARNESS" >&2
    echo "hint: omit --no-build, or build target markethub_live_smoke first" >&2
    exit 2
fi

common_args=(--mode "$MODE" --symbol "$SYMBOL" --strike-count "$STRIKE_COUNT" --config-dir "$CONFIG_DIR")
if [[ "$RAW" -eq 1 ]]; then
    common_args+=(--raw)
fi

failures=0
ran=0
skipped=0
for provider in "${PROVIDERS[@]}"; do
    if [[ "$REQUIRE_LIVE" -eq 1 ]] && ! provider_creds_available "$provider"; then
        skipped=$((skipped + 1))
        continue
    fi

    ran=$((ran + 1))
    echo "== markethub live smoke: provider=${provider} mode=${MODE} symbol=${SYMBOL} =="
    set +e
    "$HARNESS" --provider "$provider" "${common_args[@]}"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "provider=${provider} result=FAILED exit_code=${rc}" >&2
        failures=$((failures + 1))
    else
        echo "provider=${provider} result=OK"
    fi
    echo
done

if [[ "$failures" -ne 0 ]]; then
    echo "markethub live smoke completed with ${failures} provider failure(s)" >&2
    exit 1
fi

if [[ "$ran" -eq 0 && "$skipped" -ne 0 ]]; then
    exit "$SKIP_CODE"
fi

exit 0
