#!/usr/bin/env zsh
set -e

DIR="${0:A:h}"
PROJECT_ROOT="$(cd "$DIR/../.." && pwd)"
EDICT="${EDICT:-$PROJECT_ROOT/build/edict/edict}"
RUNTIME_LIB="${RUNTIME_LIB:-$PROJECT_ROOT/build/cpp-agent/libagent_runtime.so}"
RUNTIME_HDR="${RUNTIME_HDR:-$PROJECT_ROOT/cpp-agent/include/agentc_runtime/agentc_runtime.h}"
EXT_LIB="${EXT_LIB:-$PROJECT_ROOT/build/extensions/libagentc_extensions.so}"
EXT_HDR="${EXT_HDR:-$PROJECT_ROOT/extensions/agentc_stdlib.h}"
AGENTC_MODULE="${AGENTC_MODULE:-$PROJECT_ROOT/cpp-agent/edict/modules/agentc.edict}"
STATEFUL_MODULE="${STATEFUL_MODULE:-$PROJECT_ROOT/cpp-agent/edict/modules/agentc_stateful_loop.edict}"
SYSTEM_PROMPT="${SYSTEM_PROMPT:-You are a concise assistant.}"
PROMPT="${PROMPT:-Say exactly: Hello world!}"
SYSTEM_PROMPT_JSON="$(PROMPT="$SYSTEM_PROMPT" python3 - <<'PY'
import json, os
print(json.dumps(os.environ['PROMPT']))
PY
)"
PROMPT_JSON="$(PROMPT="$PROMPT" python3 - <<'PY'
import json, os
print(json.dumps(os.environ['PROMPT']))
PY
)"

echo "=== Demo: minimal stateful Edict-owned loop ==="
echo "Edict:         $EDICT"
echo "Runtime lib:   $RUNTIME_LIB"
echo "System prompt: $SYSTEM_PROMPT"
echo "Prompt:        $PROMPT"
echo

{
  cat <<EDICT
[$EXT_LIB] [$EXT_HDR] resolver.import ! @ext
[$RUNTIME_LIB] [$RUNTIME_HDR] resolver.import ! @runtimeffi
EDICT
  cat "$AGENTC_MODULE"
  echo
  cat "$STATEFUL_MODULE"
  echo
  printf '{"text": %s} @system_input\n' "$SYSTEM_PROMPT_JSON"
  printf '{"text": %s} @prompt_input\n' "$PROMPT_JSON"
  echo 'system_input.text prompt_input.text agentc_state_demo ! to_json !'
  echo 'print'
} | "$EDICT" -
