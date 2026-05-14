#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDICT_PATH="${EDICT_PATH:-$SCRIPT_DIR}"
EDICT_BUILD_DIR="${EDICT_BUILD_DIR:-$EDICT_PATH/build}"
EDICT_BIN="${EDICT_BIN:-$EDICT_BUILD_DIR/edict/edict}"
EDICT_MODULE_DIR="${EDICT_MODULE_DIR:-$EDICT_PATH/cpp-agent/edict/modules}"
EDICT_EXT_LIB="${EDICT_EXT_LIB:-$EDICT_BUILD_DIR/extensions/libagentc_extensions.so}"
EDICT_EXT_HDR="${EDICT_EXT_HDR:-$EDICT_PATH/extensions/agentc_stdlib.h}"
EDICT_RUNTIME_LIB="${EDICT_RUNTIME_LIB:-$EDICT_BUILD_DIR/cpp-agent/libagent_runtime.so}"
EDICT_RUNTIME_HDR="${EDICT_RUNTIME_HDR:-$EDICT_PATH/cpp-agent/include/agentc_runtime/agentc_runtime.h}"
EDICT_CURATED_MODULE="${EDICT_CURATED_MODULE:-$EDICT_MODULE_DIR/agentc_curated.edict}"
EDICT_AGENTC_MODULE="${EDICT_AGENTC_MODULE:-$EDICT_MODULE_DIR/agentc.edict}"
EDICT_STATEFUL_MODULE="${EDICT_STATEFUL_MODULE:-$EDICT_MODULE_DIR/agentc_stateful_loop.edict}"
EDICT_PROVIDER_CONTRACTS_MODULE="${EDICT_PROVIDER_CONTRACTS_MODULE:-$EDICT_MODULE_DIR/agentc_provider_contracts.edict}"
EDICT_AGENT_ROOT_MODULE="${EDICT_AGENT_ROOT_MODULE:-$EDICT_MODULE_DIR/agentc_agent_root.edict}"
EDICT_LLM_MODULE="${EDICT_LLM_MODULE:-$EDICT_MODULE_DIR/llm.edict}"
EDICT_DEFAULT_PRESET="${EDICT_DEFAULT_PRESET:-local-qwen}"
EDICT_AUTO_CHAT="${EDICT_AUTO_CHAT:-1}"

usage() {
  cat <<'EOF'
Usage:
  ./edict.sh                  # preload curated AgentC Edict utilities, then start REPL
  ./edict.sh -e CODE         # preload curated utilities, then execute raw Edict CODE
  ./edict.sh FILE            # preload curated utilities, then execute Edict FILE
  ./edict.sh -               # preload curated utilities, then execute Edict from stdin

Environment overrides:
  EDICT_PATH                 Project root used to derive defaults
  EDICT_BUILD_DIR            Build directory
  EDICT_BIN                  edict executable path
  EDICT_MODULE_DIR           Edict module directory
  EDICT_EXT_LIB              extensions shared library path
  EDICT_EXT_HDR              extensions header path
  EDICT_RUNTIME_LIB          runtime shared library path
  EDICT_RUNTIME_HDR          runtime header path
  EDICT_DEFAULT_PRESET       default preset for no-arg chat launcher
  EDICT_AUTO_CHAT            1 to auto-enter provider chat, 0 for raw curated REPL
EOF
}

require_path() {
  local label="$1"
  local path="$2"
  if [[ ! -e "$path" ]]; then
    printf 'Missing %s: %s\n' "$label" "$path" >&2
    exit 1
  fi
}

emit_prelude() {
  cat <<EOF
{"project_root": "$EDICT_PATH", "build_root": "$EDICT_BUILD_DIR", "edict_binary": "$EDICT_BIN", "extensions_library_path": "$EDICT_EXT_LIB", "extensions_header_path": "$EDICT_EXT_HDR", "runtime_library_path": "$EDICT_RUNTIME_LIB", "runtime_header_path": "$EDICT_RUNTIME_HDR", "module_dir": "$EDICT_MODULE_DIR", "modules": {"curated": "$EDICT_CURATED_MODULE", "agentc": "$EDICT_AGENTC_MODULE", "stateful_loop": "$EDICT_STATEFUL_MODULE", "provider_contracts": "$EDICT_PROVIDER_CONTRACTS_MODULE", "agent_root": "$EDICT_AGENT_ROOT_MODULE", "llm": "$EDICT_LLM_MODULE"}} @EDICT_PATH
EDICT_PATH.modules.curated resolver.__native.read_text!!
agentc_curated_init! /
EOF
}

emit_default_chat() {
  cat <<EOF
llm.init([$EDICT_DEFAULT_PRESET]) @provider
provider < repl! > pop /
EOF
}

run_repl() {
  if [[ "$EDICT_AUTO_CHAT" != "0" ]]; then
    {
      emit_prelude
      emit_default_chat
      cat
    } | "$EDICT_BIN" -
    return
  fi

  {
    emit_prelude
    cat
  } | "$EDICT_BIN"
}

run_stdin_script() {
  {
    emit_prelude
    cat
  } | "$EDICT_BIN" -
}

run_eval() {
  local source="$1"
  {
    emit_prelude
    printf '%s\n' "$source"
  } | "$EDICT_BIN" -
}

run_file() {
  local file_path="$1"
  if [[ ! -f "$file_path" ]]; then
    printf 'Missing script file: %s\n' "$file_path" >&2
    exit 1
  fi
  {
    emit_prelude
    cat "$file_path"
  } | "$EDICT_BIN" -
}

require_path "edict binary" "$EDICT_BIN"
require_path "curated module" "$EDICT_CURATED_MODULE"
require_path "extensions library" "$EDICT_EXT_LIB"
require_path "extensions header" "$EDICT_EXT_HDR"
require_path "runtime library" "$EDICT_RUNTIME_LIB"
require_path "runtime header" "$EDICT_RUNTIME_HDR"
require_path "agentc module" "$EDICT_AGENTC_MODULE"
require_path "stateful loop module" "$EDICT_STATEFUL_MODULE"
require_path "provider contracts module" "$EDICT_PROVIDER_CONTRACTS_MODULE"
require_path "agent root module" "$EDICT_AGENT_ROOT_MODULE"
require_path "llm module" "$EDICT_LLM_MODULE"

if [[ $# -eq 0 ]]; then
  run_repl
  exit 0
fi

case "$1" in
  -h|--help)
    usage
    ;;
  -e)
    shift
    if [[ $# -eq 0 ]]; then
      usage >&2
      exit 2
    fi
    run_eval "$*"
    ;;
  -)
    run_stdin_script
    ;;
  --ipc|--socket)
    printf 'Unsupported through edict.sh for now: %s\nUse the raw edict binary directly for IPC/socket modes.\n' "$1" >&2
    exit 2
    ;;
  *)
    run_file "$1"
    ;;
esac
