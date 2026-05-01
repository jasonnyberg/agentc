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
ROOT_MODULE="${ROOT_MODULE:-$PROJECT_ROOT/cpp-agent/edict/modules/agentc_agent_root.edict}"
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

echo "=== Demo: canonical Edict agent-root POC ==="
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
  cat "$ROOT_MODULE"
  echo
  printf '{"text": %s} @system_input\n' "$SYSTEM_PROMPT_JSON"
  printf '{"text": %s} @prompt1_input\n' "$PROMPT1_JSON"
  printf '{"text": %s} @prompt2_input\n' "$PROMPT2_JSON"
  echo "[$CONFIG] agentc_read_json_file ! @runtime_config"
  echo 'system_input.text runtime_config agentc_agent_root_init_with_runtime ! @root'
  echo 'root agentc_agent_root_create_runtime ! @runtime'
  echo 'runtime root prompt1_input.text agentc_agent_root_turn ! @root'
  echo 'runtime root prompt2_input.text agentc_agent_root_turn ! @root'
  echo 'runtime agentc_destroy ! /'
  echo 'root to_json !'
  echo 'print'
} | "$EDICT" -
