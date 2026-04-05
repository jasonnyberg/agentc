#!/usr/bin/env bash
set -euo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$DIR")"
EDICT="${EDICT:-$PROJECT_ROOT/build/edict/edict}"
SDL_DEMO_LIB="${SDL_DEMO_LIB:-$PROJECT_ROOT/build/demo/libagentc_sdl_triangle_demo.so}"
SDL3_ROOT="${SDL3_ROOT:-/home/jwnyberg/SDL3/SDL3-3.4.4/install}"
SCRIPT="${SCRIPT:-$DIR/demo_sdl_triangle.ed}"

if [[ ! -x "$EDICT" ]]; then
  echo "Missing Edict interpreter at $EDICT"
  exit 1
fi

if [[ ! -f "$SDL_DEMO_LIB" ]]; then
  echo "Missing SDL demo bridge at $SDL_DEMO_LIB"
  echo "Reconfigure with SDL3 available and AGENTC_WITH_SDL_DEMO=ON."
  exit 1
fi

echo "Running AgentC SDL triangle demo..."
cd "$PROJECT_ROOT"
export LD_LIBRARY_PATH="$SDL3_ROOT/lib64:$SDL3_ROOT/lib:${LD_LIBRARY_PATH:-}"
"$EDICT" "$SCRIPT"
