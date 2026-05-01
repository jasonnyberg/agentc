#!/usr/bin/env zsh
set -e

DIR="${0:A:h}"
PROJECT_ROOT="$(cd "$DIR/../.." && pwd)"
EDICT="${EDICT:-$PROJECT_ROOT/build/edict/edict}"
RUNTIME_LIB="${RUNTIME_LIB:-$PROJECT_ROOT/build/cpp-agent/libagent_runtime.so}"
RUNTIME_HDR="${RUNTIME_HDR:-$PROJECT_ROOT/cpp-agent/include/agentc_runtime/agentc_runtime.h}"
EXT_LIB="${EXT_LIB:-$PROJECT_ROOT/build/extensions/libagentc_extensions.so}"
EXT_HDR="${EXT_HDR:-$PROJECT_ROOT/extensions/agentc_stdlib.h}"
MODULE="${MODULE:-$PROJECT_ROOT/cpp-agent/edict/modules/agentc.edict}"

if [[ ! -x "$EDICT" ]]; then
  echo "Missing edict binary: $EDICT" >&2
  exit 1
fi
if [[ ! -f "$RUNTIME_LIB" ]]; then
  echo "Missing runtime library: $RUNTIME_LIB" >&2
  exit 1
fi
if [[ ! -f "$EXT_LIB" ]]; then
  echo "Missing extensions library: $EXT_LIB" >&2
  exit 1
fi

echo "=== Demo: Edict wrapper over libagent_runtime.so ==="
echo "Edict:        $EDICT"
echo "Runtime lib:  $RUNTIME_LIB"
echo "Runtime hdr:  $RUNTIME_HDR"
echo "Ext lib:      $EXT_LIB"
echo "Module:       $MODULE"
echo

{
  cat <<EDICT
[$EXT_LIB] [$EXT_HDR] resolver.import ! @ext
[$RUNTIME_LIB] [$RUNTIME_HDR] resolver.import ! @runtimeffi
EDICT
  cat "$MODULE"
  cat <<'EDICT'
{"default_provider": "google", "default_model": "gemini-2.5-pro"} agentc_runtime_create ! @rt
rt {} agentc_call ! to_json !
print
rt agentc_destroy ! /
EDICT
} | "$EDICT" -
