# Workproduct: Runtime-Adaptive Data Visualization Engine

## Goal
Design a native, high-performance, fault-tolerant visualization engine that allows LLMs to dynamically generate, compile, and execute C code on the fly to manipulate data visualizations, leveraging the `agentc` microkernel and `mmap` slab architecture.

## Context
The project utilizes `agentc` with a custom hybrid VM, `listree` data structures, and file-backed `mmap` slabs for memory management. The goal is to avoid high-overhead serialization or traditional FFI boundaries by executing dynamic code closer to the hardware/memory layer.

## Proposed Architecture

### 1. In-Memory Compilation (libtcc)
Instead of invoking external compilers or `dlopen` for every change, embed `libtcc` into a specialized Micro-VM.
*   **Mechanism**: `libtcc` compiles LLM-generated ANSI C strings directly into the process's executable memory.
*   **Benefit**: Returns a raw function pointer (`void*`) that can be called with zero FFI overhead, effectively running at native CPU speed.

### 2. Compilation Micro-VM Service
To isolate the core VM from potentially unstable LLM-generated code:
*   **Isolation**: This micro-VM acts as an isolated factory. It receives C code strings via the `vm0` broker, compiles them using `libtcc`, and executes them.
*   **Fault Tolerance**: If the LLM generates a segmentation fault or memory error, only this worker process crashes. The core `vm0` broker and the main rendering loop remain operational.

### 3. Zero-Copy Data Mutation via Slabs
Bypass standard IPC serialization for visualization buffers:
*   **Shared Memory**: The graphics canvas process and the worker VM both map the same file-backed `mmap` slabs.
*   **Direct Mutation**: The dynamic C function receives a raw pointer to the slab memory, modifying pixel/vertex data in-place.
*   **Hardware Hand-off**: The Canvas Renderer process detects the update (via `vm0` notification) and immediately pushes the raw buffer to the GPU (Vulkan/OpenGL/Raylib), enabling 60+ FPS rendering independent of the compilation/mutation cadences.

## Technical Constraints & Decisions
*   **Language Constraint**: Limit dynamic compilation to ANSI C (C99) for predictability and memory safety, avoiding the complexities of C++ templates or exceptions.
*   **Microkernel Messaging**: Use the existing `listree` and `vm0` broker mechanism for passing slab reference tokens, not the data payloads themselves.
*   **Future Path**: Once the zero-copy C path is solidified, investigate compiling GLSL/Vulkan shader strings on-the-fly to fully offload visualization math to the GPU.

## Status
**Proposal/Design Phase**
- [ ] Implement `libtcc` prototype within a sandbox worker.
- [ ] Define slab-sharing interface between Worker VM and Graphics Canvas.
- [ ] Establish `vm0` messaging protocol for slab updates.

## References
- `ffi_alternative.txt` (Discussion transcripts)
- `agentc` repository internal documentation
