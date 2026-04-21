# Session 1440-1440

## Summary

Re-spun the SDL demo into a mostly native proof of concept that imports real SDL3 functions directly, eliminating the custom bridge for the basic triangle-outline case.

## Work Completed

- Investigated the current FFI path and confirmed that Cartographer already supports opaque pointer returns and pointer arguments as binary values, which is enough for `SDL_Window*` and `SDL_Renderer*` lifetimes.
- Identified the real blockers to fully raw SDL authoring in Edict: `char*` string construction/ownership, struct-array packing (for APIs like `SDL_RenderGeometry`), and union-heavy event surfaces.
- Added `demo/demo_sdl3_native_poc.h` as a minimal handwritten import header that declares only the SDL3 calls needed for a direct native POC.
- Added `demo/demo_sdl_triangle_native.sh`, which imports the local `libSDL3.so` directly and renders a triangle outline with no custom bridge library.
- Updated `README.md` to describe the new native POC path and the current direct-import boundary.

## Verification

- `bash -n demo/demo_sdl_triangle_native.sh`
- `./demo/demo_sdl_triangle_native.sh`

## Outcome

The project now has both ends of the SDL story: a bridge-backed demo for richer capability shaping and a genuinely bridge-free native SDL3 proof of concept for the subset of the API that matches AgentC's current FFI model. This gives a clearer line for future work: expand direct import only where the calling convention stays pointer-and-scalar friendly, and use tiny helpers only for the irreducible gaps.

## Links

- Goal: 🔗[`G055-NativeSdlPoc`](../../../../../Goals/G055-NativeSdlPoc/index.md)
- Prior SDL goal: 🔗[`G054-SdlImportDemo`](../../../../../Goals/G054-SdlImportDemo/index.md)
- Dashboard: 🔗[`LocalContext/Dashboard.md`](../../../../../../Dashboard.md)
- Native POC script: 🔗[`demo/demo_sdl_triangle_native.sh`](../../../../../../../demo/demo_sdl_triangle_native.sh)
- Native import header: 🔗[`demo/demo_sdl3_native_poc.h`](../../../../../../../demo/demo_sdl3_native_poc.h)
