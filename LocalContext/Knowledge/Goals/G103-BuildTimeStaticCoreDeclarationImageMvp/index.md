# Goal: G103 — Build-Time Static Core Declaration Image MVP

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G096 — Authoritative mmap Session Resume](../G096-AuthoritativeMmapSessionResume/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Prove the first build-time static core image slice: generate a small, versioned, read-only slab/Listree image that contains declarative module/import metadata but no process-local native handles, then mount/read it at runtime.

## Rationale
The full-send slab architecture should not start with a full allocator rewrite. The safer first substrate proof is a **static declaration image**: a build artifact that declares library/module contents as Listree data, validates its manifest/hash/version, and can be mmap-installed by a Root1-style runtime without repeating dynamic import construction.

## Scope
In scope:
- one tiny static declaration image for one module or import namespace;
- a manifest with format/version/hash/root-id fields;
- declarative metadata only: namespace shape, signatures, safety metadata, expected library identity/symbols;
- runtime mount/read/inspect path;
- test coverage proving the image can be generated and consumed without live native handles.

Out of scope:
- full static core image for all builtins/modules;
- process-isolated micro-VM launch;
- published result slabs;
- general layered slab allocator semantics;
- storing raw `dlopen`/`dlsym` handles in static slabs.

## Implementation Plan
- [ ] Define the minimal static declaration image manifest schema.
- [ ] Pick one small import/module surface as the first image payload.
- [ ] Add a build-time image generator or deterministic test generator that emits slab files plus manifest.
- [ ] Ensure the image contains no process-local handles or mutable VM activation state.
- [ ] Add runtime inspection/mount support sufficient to read the static declaration namespace.
- [ ] Add stale/incompatible manifest validation.
- [ ] Add checked-in tests around generation, manifest validation, and runtime readback.

## Acceptance Criteria
- [ ] A static declaration image can be generated deterministically from source metadata.
- [ ] Runtime code can mmap/read the image as read-only Listree/slab data.
- [ ] The image manifest records enough version/hash/root-id information to detect stale or incompatible artifacts.
- [ ] No raw native handles are stored as authoritative shared state.
- [ ] Documentation explains the distinction between declarative import image and lazy process-local native binding.

## Notes
This goal is intentionally narrower than full Root0/Root1. It creates the first durable artifact that later Root1 and micro-VM workers can mount.
