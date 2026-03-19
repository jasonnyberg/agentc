#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EDICT_BIN="${EDICT_BIN:-"$ROOT_DIR/build/edict/edict"}"
VISUALIZE_BIN="${VISUALIZE_BIN:-"$ROOT_DIR/build/cartographer/j3visualize"}"

if [[ ! -x "$EDICT_BIN" ]]; then
  echo "Missing edict binary at $EDICT_BIN" >&2
  echo "Build first: CCACHE_DISABLE=1 cmake --build build" >&2
  exit 1
fi

run_case() {
  local name="$1"
  local code="$2"
  echo "== $name =="
  echo "code: $code"
  "$EDICT_BIN" -e "$code"
  echo
}

run_case "Literal string" "[hello]"
run_case "Numbers" "1 2 3"
run_case "Auto-deref missing symbol" "not_defined"
run_case "Assign and deref" "[hello]@x x"
run_case "Remove symbol" "[hello]@x /x x"
run_case "Remove TOS (/)" "1 2 3 /"
run_case "Stack ops (dup/swap/pop)" "1 2 dup swap pop"
run_case "Eval literal" "[hello] !"
run_case "Nested eval" "[[hello] !] !"
run_case "Method call eval" "[!]@myeval [dup]@twice myeval([hello] twice)"
run_case "Print side-effect" "[hello] print"

echo "== Cartographer dot output =="
HEADER_PATH="$ROOT_DIR/cartographer/tests/test_input.h"
DOT_OUT="$ROOT_DIR/doc/cartographer-demo.dot"
if [[ -x "$VISUALIZE_BIN" && -f "$HEADER_PATH" ]]; then
  "$VISUALIZE_BIN" "$HEADER_PATH" "$DOT_OUT"
  echo "dot file: $DOT_OUT"
  sed -n '1,20p' "$DOT_OUT"
else
  echo "skipped: missing $VISUALIZE_BIN or $HEADER_PATH"
fi
