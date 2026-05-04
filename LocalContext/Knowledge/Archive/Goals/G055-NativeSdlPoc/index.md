# G055 - Native SDL POC

## Status: COMPLETE

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Prior SDL bridge work: 🔗[`G054-SdlImportDemo`](../G054-SdlImportDemo/index.md)

## Goal

Replace as much of the SDL demo bridge as possible with direct Cartographer import of real SDL3 functions, allowing only minor standalone utilities if the raw API surface proves too awkward.

## Why This Matters

- A direct-import POC is a stronger demonstration of AgentC's native capability model than a purpose-built bridge library.
- It clarifies the current boundary between what Cartographer/Edict can already express and what still needs helper code.

## Navigation Guide For Agents

- Prefer a real imported SDL3 symbol over a helper wrapper whenever the calling convention is already expressible in Edict.
- If helper code is still needed, keep it tightly scoped to one irreducible mismatch such as event-union handling or struct-array packing.

## Result

- Added `demo/demo_sdl3_native_poc.h`, a tiny handwritten SDL3 import surface with opaque pointer typedefs and only the functions needed for a direct triangle-outline proof of concept.
- Added `demo/demo_sdl_triangle_native.sh`, which imports `libSDL3.so` directly through Cartographer and renders a triangle using `SDL_CreateWindow`, `SDL_CreateRenderer`, `SDL_SetRenderDrawColor`, `SDL_RenderLine`, `SDL_RenderPresent`, and cleanup calls.
- Confirmed the direct-import path runs without any custom bridge library.

## Findings

- AgentC can already drive a meaningful subset of SDL3 natively when the foreign surface is limited to opaque pointers plus scalar arguments and returns.
- Pointer-returning functions like `SDL_CreateWindow(...)` and `SDL_CreateRenderer(...)` work because the FFI already wraps raw pointers as opaque binary values that Edict can store and pass back.
- The main remaining direct-import limitations are real `char*` string ownership, complex structs/arrays such as `SDL_Vertex[]`, and union-heavy/event-heavy APIs like `SDL_Event`.
- For a simple triangle outline, no helper code is required. A filled triangle or interactive event loop will likely want either struct-packing support or a very small utility layer.

## Verification

- `bash -n demo/demo_sdl_triangle_native.sh`
- `./demo/demo_sdl_triangle_native.sh`
