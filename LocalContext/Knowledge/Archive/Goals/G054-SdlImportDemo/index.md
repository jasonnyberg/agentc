# G054 - SDL Import Demo

## Status: COMPLETE

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Language/runtime architecture reference: 🔗[`README.md`](../../../../README.md)
- FFI/runtime import semantics reference: 🔗[`LocalContext/Knowledge/WorkProducts/edict_language_reference.md`](../../WorkProducts/edict_language_reference.md)
- Builds on the imported-capability model documented in 🔗[`G048-LibraryBackedLogicCapability`](../G048-LibraryBackedLogicCapability/index.md)

## Goal

Review AgentC's semantics and architecture through the lens of external capability import, then add a demo that uses SDL to show non-trivial native-library interaction from Edict.

## Why This Matters

- AgentC's core claim is that native capabilities become first-class words at runtime through Cartographer rather than generated bindings.
- A small SDL-backed graphics demo is a better proof point than math-only FFI because it exercises stateful native resources, multiple imported calls, and a recognizable result.
- SDL's real-world API surface is complex enough that it also tests where AgentC benefits from a Cartographer-friendly shim instead of exposing the raw library surface directly.

## Scope

- Review the current Edict semantics and Cartographer import model relevant to external libraries.
- Add a minimal SDL-backed demo capability and Edict script/shell wrapper.
- Keep the new demo optional so the core build still succeeds when SDL is unavailable.

## Expected Work Product

- A concise architecture review grounded in README/reference/code.
- An optional SDL demo library plus runnable Edict demo script.
- Build wiring and usage notes for environments that have SDL3 installed.

## Result

- Built SDL3 locally from `/home/jwnyberg/SDL3/SDL3-3.4.4` and used that install as the demo dependency source.
- Added an optional SDL3-backed bridge library in `demo/` with a Cartographer-friendly C surface instead of trying to import raw SDL directly.
- Added `demo/demo_sdl_triangle.ed` and `demo/demo_sdl_triangle.sh` so Edict imports the bridge through normal `resolver.import !` flow and draws a triangle in a window when the optional library is available.
- Updated `demo/CMakeLists.txt` and `README.md` so the SDL demo is opt-in via `AGENTC_WITH_SDL_DEMO=ON`, finds SDL3 through `AGENTC_SDL3_ROOT`, and does not affect default builds.

## Architecture Findings

- Edict's semantics are intentionally minimal and uniform: words push values or execute capabilities, and `!` is the single invocation boundary for thunks, builtins, and imported native functions.
- The real architectural center is the imported-capability model: Cartographer parses C headers, resolves symbols, and materializes callable words inside the live Listree runtime without generated glue code.
- AgentC is strongest when imported libraries are exposed through small capability-oriented surfaces. That matches the current logic-runtime detachment work and avoids over-fitting the language to one foreign API's raw header complexity.
- SDL is a good demo target precisely because it shows where a thin bridge helps: the raw SDL API includes opaque handles, macros, event unions, and pointer-heavy state that are awkward as a first direct-import example.

## Verification

- `cmake -S /home/jwnyberg/SDL3/SDL3-3.4.4 -B /home/jwnyberg/SDL3/SDL3-3.4.4/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/home/jwnyberg/SDL3/SDL3-3.4.4/install -DSDL_ALSA=OFF -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_X11_XCURSOR=OFF -DSDL_X11_XINPUT=OFF -DSDL_X11_XFIXES=OFF -DSDL_X11_XDBE=OFF -DSDL_X11_XRANDR=OFF -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XSHAPE=OFF -DSDL_X11_XSYNC=OFF -DSDL_X11_XTEST=OFF`
- `cmake --build /home/jwnyberg/SDL3/SDL3-3.4.4/build -j2 && cmake --install /home/jwnyberg/SDL3/SDL3-3.4.4/build`
- `cmake -B build -DAGENTC_WITH_SDL_DEMO=ON -DAGENTC_SDL3_ROOT=/home/jwnyberg/SDL3/SDL3-3.4.4/install`
- `cmake --build build -j2 --target agentc_sdl_triangle_demo edict`
- `bash -n demo/demo_sdl_triangle.sh`
- `./demo/demo_sdl_triangle.sh`

## Navigation Guide For Agents

- Prefer a thin Cartographer-friendly bridge over trying to import the entire raw SDL header surface in the first demo.
- Preserve the existing imported-capability story: Edict should call native words through normal `resolver.import !` flow.
