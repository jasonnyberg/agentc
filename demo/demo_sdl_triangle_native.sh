#!/usr/bin/env bash
set -euo pipefail

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$DIR")"
EDICT="${EDICT:-$PROJECT_ROOT/build/edict/edict}"
SDL3_ROOT="${SDL3_ROOT:-/home/jwnyberg/SDL3/SDL3-3.4.4/install}"
SDL3_LIB="${SDL3_LIB:-$SDL3_ROOT/lib64/libSDL3.so}"
SDL3_HDR="${SDL3_HDR:-$DIR/demo_sdl3_native_poc.h}"
EXT_LIB="${EXT_LIB:-$PROJECT_ROOT/build/extensions/libagentc_extensions.so}"
EXT_HDR="${EXT_HDR:-$PROJECT_ROOT/extensions/agentc_stdlib.h}"

if [[ ! -x "$EDICT" ]]; then
  echo "Missing Edict interpreter at $EDICT"
  exit 1
fi

if [[ ! -f "$SDL3_LIB" ]]; then
  echo "Missing SDL3 library at $SDL3_LIB"
  exit 1
fi

if [[ ! -f "$SDL3_HDR" ]]; then
  echo "Missing SDL3 POC header at $SDL3_HDR"
  exit 1
fi

if [[ ! -f "$EXT_LIB" ]]; then
  echo "Missing extensions library at $EXT_LIB"
  exit 1
fi

if [[ ! -f "$EXT_HDR" ]]; then
  echo "Missing extensions header at $EXT_HDR"
  exit 1
fi

echo "Running AgentC native SDL3 triangle POC..."
cd "$PROJECT_ROOT"
export LD_LIBRARY_PATH="$SDL3_ROOT/lib64:$SDL3_ROOT/lib:${LD_LIBRARY_PATH:-}"

"$EDICT" - <<EDICT
unsafe_extensions_allow ! pop
[$EXT_LIB] [$EXT_HDR] resolver.import ! @ext
[$SDL3_LIB] [$SDL3_HDR] resolver.import ! @sdl

'AgentC Native Triangle ext.agentc_ext_string_to_cstr_ltv ! @title
'32 sdl.SDL_Init ! /

'float ext.agentc_ext_type_size_ltv ! @f32

title '640 '480 '0 sdl.SDL_CreateWindow ! @window
title ext.agentc_ext_memory_free ! /
window 'default sdl.SDL_CreateRenderer ! @renderer

renderer '244 '240 '232 '255 sdl.SDL_SetRenderDrawColor ! /
renderer sdl.SDL_RenderClear ! /

'24 ext.agentc_ext_memory_alloc ! @xy
xy '0 f32 '0 'float '320.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /
xy '0 f32 '4 'float '96.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /
xy '1 f32 '0 'float '144.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /
xy '1 f32 '4 'float '408.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /
xy '2 f32 '0 'float '544.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /
xy '2 f32 '4 'float '408.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /

'48 ext.agentc_ext_memory_alloc ! @colors
colors '0 '16 '0 'float '0.094 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '0 '16 '4 'float '0.361 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '0 '16 '8 'float '0.580 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '0 '16 '12 'float '1.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '1 '16 '0 'float '0.094 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '1 '16 '4 'float '0.361 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '1 '16 '8 'float '0.580 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '1 '16 '12 'float '1.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '2 '16 '0 'float '0.094 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '2 '16 '4 'float '0.361 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '2 '16 '8 'float '0.580 ext.agentc_ext_memory_write_array_scalar_ltv ! /
colors '2 '16 '12 'float '1.0 ext.agentc_ext_memory_write_array_scalar_ltv ! /

'float '320.0 ext.agentc_ext_binary_pack_scalar_ltv ! @x1bin
'float '96.0 ext.agentc_ext_binary_pack_scalar_ltv ! @y1bin
x1bin y1bin ext.agentc_ext_binary_concat_ltv ! @p1bin
p1bin '0 f32 ext.agentc_ext_binary_slice_ltv ! @x1slice
p1bin f32 f32 ext.agentc_ext_binary_slice_ltv ! @y1slice
x1slice '0 'float ext.agentc_ext_binary_view_scalar_ltv ! @x1
y1slice '0 'float ext.agentc_ext_binary_view_scalar_ltv ! @y1

xy '1 f32 '0 'float ext.agentc_ext_memory_read_array_scalar_ltv ! @x2
xy '1 f32 '4 'float ext.agentc_ext_memory_read_array_scalar_ltv ! @y2

renderer 'null xy '8 colors '16 'null '0 '3 'null '0 '0 sdl.SDL_RenderGeometryRaw ! /

renderer '18 '70 '118 '255 sdl.SDL_SetRenderDrawColor ! /
x1 y1 '144.0 '408.0 sdl.SDL_RenderLine ! /
x2 y2 '544.0 '408.0 sdl.SDL_RenderLine ! /
renderer '544.0 '408.0 x1 y1 sdl.SDL_RenderLine ! /

renderer sdl.SDL_RenderPresent ! /
'1800 sdl.SDL_Delay ! /

xy ext.agentc_ext_memory_free ! /
colors ext.agentc_ext_memory_free ! /
renderer sdl.SDL_DestroyRenderer ! /
window sdl.SDL_DestroyWindow ! /
sdl.SDL_Quit ! /
EDICT
