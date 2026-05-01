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
CONFIG="${CONFIG:-$PROJECT_ROOT/agentc-config.json}"
SYSTEM_PROMPT="${SYSTEM_PROMPT:-You are a concise assistant.}"
PROMPT1="${PROMPT1:-Say exactly: Hello world!}"
PROMPT2="${PROMPT2:-What did you just say?}"
SYSTEM_PROMPT_JSON="$(PROMPT="$SYSTEM_PROMPT" python3 - <<'PY'
import json, os
print(json.dumps(os.environ['PROMPT']))
PY
)"
PROMPT1_JSON="$(PROMPT="$PROMPT1" python3 - <<'PY'
import json, os
print(json.dumps(os.environ['PROMPT']))
PY
)"
PROMPT2_JSON="$(PROMPT="$PROMPT2" python3 - <<'PY'
import json, os
print(json.dumps(os.environ['PROMPT']))
PY
)"

echo "=== Demo: stateful Edict-owned loop with conversation history ==="
echo "Edict:         $EDICT"
echo "Runtime lib:   $RUNTIME_LIB"
echo "Config:        $CONFIG"
echo "System prompt: $SYSTEM_PROMPT"
echo "Prompt 1:      $PROMPT1"
echo "Prompt 2:      $PROMPT2"
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
  printf '{"text": %s} @prompt1_input\n' "$PROMPT1_JSON"
  printf '{"text": %s} @prompt2_input\n' "$PROMPT2_JSON"
  echo "[$CONFIG] @config_path"
  echo 'config_path agentc_runtime_create_path ! @runtime'
  echo 'system_input.text agentc_state_init ! @state'
  echo 'runtime state prompt1_input.text agentc_state_turn ! @state'
  echo 'runtime state prompt2_input.text agentc_state_turn ! @state'
  echo 'runtime agentc_destroy ! /'
  echo 'state to_json !'
  echo 'print'
} | "$EDICT" -
