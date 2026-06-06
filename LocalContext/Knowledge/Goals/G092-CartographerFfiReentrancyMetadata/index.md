# Goal: G092 — Cartographer FFI Re-entrancy Metadata

**Status**: ACTIVE / DESIGN PHASE  
**Created**: 2026-05-14  
**Parent**: 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md)
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Define capability metadata for imported Cartographer/FFI libraries so that worker VMs can safely import native capabilities through static declaration images without inheriting raw process-local handles.

## Rationale
Static core slabs should store **declarative import metadata** (namespace shape, signatures, safety/re-entrancy classification, required library identity, version constraints) rather than process-local `dlopen` handles. Actual native binding is lazy and per-process: the static image declares the capability; each worker process resolves and binds it locally, validating metadata against the loaded library.

This goal is now active because G110's resource broker design established the capability metadata hooks needed for this schema.
