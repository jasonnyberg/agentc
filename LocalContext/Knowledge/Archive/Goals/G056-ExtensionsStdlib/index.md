# G056 - Extensions Stdlib

## Status: COMPLETE

## Parent Context

- Parent project context: 🔗[`LocalContext/Dashboard.md`](../../../Dashboard.md)
- Builds on the SDL experiments in 🔗[`G054-SdlImportDemo`](../G054-SdlImportDemo/index.md) and 🔗[`G055-NativeSdlPoc`](../G055-NativeSdlPoc/index.md)

## Goal

Create an `extensions/` directory containing generic helper libraries that act as an early AgentC stdlib, filling practical gaps such as string-to-pointer conversion, memory allocation, and binary packing without bloating the VM core.

## Why This Matters

- AgentC intentionally keeps the VM and language core minimal, so a growing ecosystem of importable helper libraries is the natural place to add practical programming-language utilities.
- A structured `extensions/` tree also provides a reusable pattern for future capability modules instead of one-off demo-specific bridge code.

## Navigation Guide For Agents

- Keep the helpers general-purpose and library-agnostic.
- Prefer LTV-based APIs when Edict strings or values must cross into native code without first becoming raw C pointers.

## Result

- Added an `extensions/` directory with `agentc_stdlib.h`, `agentc_stdlib.cpp`, `CMakeLists.txt`, and a small README that frames the directory as an early stdlib layer rather than demo-only glue.
- Implemented reusable helpers for memory allocation/freeing, LTV string to owned C-string conversion, scalar packing into raw memory, scalar reads from raw memory, array/struct-style indexed field access, pointer slicing, and typed binary packing/slicing/views.
- Built the helper library as `libagentc_extensions.so` and updated the native SDL3 POC to load both the extensions library and SDL3, proving the intended multi-library import model.
- Used the generic helpers to remove SDL-specific bridge logic from the filled-triangle path: the demo now allocates raw float/color buffers through the extensions library and calls `SDL_RenderGeometryRaw` directly.
- Expanded the native SDL POC to exercise the new generic helpers: indexed array writes populate vertex/color buffers, typed array reads pull values back out, and binary pack/slice/view helpers round-trip vertex components without any SDL-specific support code.

## Findings

- A thin importable stdlib layer is a good fit for AgentC's architecture: it fills practical programming-language gaps without pushing those concerns into the VM core.
- LTV-based helper signatures are the key design pattern because they let Edict pass native strings and values into helper code without first solving raw `char*` construction in the core FFI.
- The combination of `agentc_extensions` plus direct SDL3 import is materially better than a monolithic SDL bridge: generic buffer/string utilities can be reused by many foreign libraries.
- The next useful abstraction level is typed buffer/view tooling rather than more ad hoc demo helpers. That keeps the stdlib general-purpose while still making foreign struct-array APIs tractable.

## Verification

- `cmake -B build -DAGENTC_WITH_SDL_DEMO=ON -DAGENTC_SDL3_ROOT=/home/jwnyberg/SDL3/SDL3-3.4.4/install`
- `cmake --build build -j2 --target agentc_extensions edict`
- `bash -n demo/demo_sdl_triangle_native.sh`
- `./demo/demo_sdl_triangle_native.sh`
