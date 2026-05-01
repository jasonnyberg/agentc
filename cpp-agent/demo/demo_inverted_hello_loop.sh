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
HELLO_LOOP_MODULE="${HELLO_LOOP_MODULE:-$PROJECT_ROOT/cpp-agent/edict/modules/agentc_hello_loop.edict}"
PROMPT="${PROMPT:-Say exactly: Hello world!}"
PROMPT_JSON="$(PROMPT="$PROMPT" python3 - <<'PY'
import json, os
print(json.dumps(os.environ['PROMPT']))
PY
)"

echo "=== Demo: minimal inverted Edict-owned hello loop ==="
echo "Edict:       $EDICT"
echo "Runtime lib: $RUNTIME_LIB"
echo "Prompt:      $PROMPT"
echo

{
  cat <<EDICT
[$EXT_LIB] [$EXT_HDR] resolver.import ! @ext
[$RUNTIME_LIB] [$RUNTIME_HDR] resolver.import ! @runtimeffi
EDICT
  cat "$AGENTC_MODULE"
  echo
  cat "$HELLO_LOOP_MODULE"
  echo
  printf '{"text": %s} @input\n' "$PROMPT_JSON"
  echo 'input.text agentc_hello_demo !'
  echo 'print'
} | "$EDICT" -
