# Session 1526-1526

## Summary

Created a new `extensions/` directory as the start of an AgentC stdlib layer, then used it to replace demo-specific SDL glue with reusable memory/string/binary helpers plus direct SDL3 calls.

## Work Completed

- Opened G056 to capture the new direction: importable generic helper libraries rather than one-off bridge code.
- Added `extensions/agentc_stdlib.h` and `extensions/agentc_stdlib.cpp` with reusable helpers for memory allocation, LTV-to-C-string conversion, raw-memory scalar packing, raw-memory scalar reads, and binary concatenation.
- Added `extensions/CMakeLists.txt` and wired the top-level build to produce `build/extensions/libagentc_extensions.so`.
- Added `extensions/README.md` to frame the directory as an early stdlib layer and establish design rules for future extension modules.
- Updated the native SDL3 demo so it now loads both `libagentc_extensions.so` and `libSDL3.so`; the demo allocates raw vertex/color buffers through the generic extensions library and calls `SDL_RenderGeometryRaw` directly.

## Verification

- `cmake -B build -DAGENTC_WITH_SDL_DEMO=ON -DAGENTC_SDL3_ROOT=/home/jwnyberg/SDL3/SDL3-3.4.4/install`
- `cmake --build build -j2 --target agentc_extensions edict`
- `bash -n demo/demo_sdl_triangle_native.sh`
- `./demo/demo_sdl_triangle_native.sh`

## Outcome

The project now has the first structured step toward an AgentC stdlib outside the VM core. The SDL POC demonstrates the intended architecture: generic helper modules provide the missing low-level language facilities, while substantial foreign libraries like SDL3 are still imported and called directly through Cartographer.

## Links

- Goal: 🔗[`G056-ExtensionsStdlib`](../../../../../Goals/G056-ExtensionsStdlib/index.md)
- Prior goal: 🔗[`G055-NativeSdlPoc`](../../../../../Goals/G055-NativeSdlPoc/index.md)
- Dashboard: 🔗[`LocalContext/Dashboard.md`](../../../../../../Dashboard.md)
- Extensions dir: 🔗[`extensions/README.md`](../../../../../../../extensions/README.md)
- Native SDL demo: 🔗[`demo/demo_sdl_triangle_native.sh`](../../../../../../../demo/demo_sdl_triangle_native.sh)
