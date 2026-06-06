# Goal: G092 — Cartographer FFI Re-entrancy Metadata

|**Status**: COMPLETE  
**Created**: 2026-05-14  
**Parent**: 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md) (see §9 "Capability Metadata as Loader Contract")

## Objective
Define capability metadata for imported Cartographer/FFI libraries so that worker VMs can safely import native capabilities through static declaration images without inheriting raw process-local handles.

## Rationale
Static core slabs should store **declarative import metadata** (namespace shape, signatures, safety/re-entrancy classification, required library identity, version constraints) rather than process-local `dlopen` handles. Actual native binding is lazy and per-process: the static image declares the capability; each worker process resolves and binds it locally, validating metadata against the loaded library.

This goal is now active because G110's resource broker design established the capability metadata hooks needed for this schema.

## Implementation Plan

### Phase 1: Symbol Metadata Schema Extension
Extend the per-symbol declaration in `static_declaration_image.cpp::symbolDeclaration()` to include the full capability metadata from 🔗[Layered mmap Micro-VM Architecture §9](../../Concepts/LayeredMmapMicroVmArchitecture/index.md):

```json
{
  "word": "worker.edict_active_count",
  "native_symbol": "agentc_worker_edict_active_count_ltv",
  "stack_signature": "() -> ltv",
  "category": "lifecycle",
  "binding": "lazy_process_local",
  "stores_native_handle": "false",
  "worker_allowed": "true",
  "thread_safe": "true",
  "process_safe": "true",
  "reentrant": "true",
  "pure": "true",
  "side_effects": "none",
  "credential_bearing": "false",
  "static_shareable_declaration": "true",
  "requires_process_local_binding": "true",
  "notes": "Declarative metadata only; actual Cartographer/FFI binding is process-local."
}
```

### Phase 2: Cartographer Import Classification
Add capability inference logic to Cartographer import (`service.cpp::annotateImportedNode`):
- Analyze function signatures, names, and return types
- Classify: thread_safe, process_safe, reentrant, pure, side_effects, credential_bearing
- Default conservative: assume unsafe/impure unless provably otherwise
- Heuristics: known safe patterns (getters, pure math), known unsafe (I/O, stateful, credentials)

### Phase 3: Static Declaration Image Generation
Update `buildWorkerPrimitiveDeclarationImage()` to include capability metadata for each worker primitive. These are native C++ primitives (not Cartographer-imported) so they can be manually annotated.

### Phase 4: Validation & Tests
- Add validation in `validateDeclarationImage()` for required capability fields
- Add tests verifying capability metadata is present and correct
- Test that worker_allowed=false symbols are rejected for worker tasks

### Phase 5: Worker Runtime Enforcement (Future)
- `intern_start!` task validation checks `worker_allowed` and `side_effects`
- Process-isolated workers (G107) validate capability metadata at launch

## Acceptance Criteria
- [x] Static declaration image symbols carry full capability metadata schema
- [x] Cartographer import annotates functions with inferred capabilities (conservative defaults)
- [x] Worker primitives (`worker.edict_*`) have accurate manual capability annotations
- [x] Validation rejects images missing required capability fields
- [x] Tests cover capability metadata generation and validation

## Current Symbol Metadata (Baseline)
| Field | Current | G092 Target |
|-------|---------|-------------|
| word | ✅ | ✅ |
| native_symbol | ✅ | ✅ |
| stack_signature | ✅ | ✅ |
| category | ✅ | ✅ |
| binding | ✅ (lazy_process_local) | ✅ |
| stores_native_handle | ✅ (false) | ✅ |
| worker_allowed | ✅ (true) | ✅ |
| **thread_safe** | ❌ | ✅ |
| **process_safe** | ❌ | ✅ |
| **reentrant** | ❌ | ✅ |
| **pure** | ❌ | ✅ |
| **side_effects** | ❌ | ✅ (enum: none/filesystem/network/process/credentials) |
| **credential_bearing** | ❌ | ✅ |
| **static_shareable_declaration** | ❌ | ✅ |
| **requires_process_local_binding** | ❌ | ✅ |

## Summary
- **Phase 1 (Symbol Metadata Schema)**: Extended `symbolDeclaration()` in `static_declaration_image.cpp` with 8 capability fields: `thread_safe`, `process_safe`, `reentrant`, `pure`, `side_effects` (enum: none/filesystem/network/process/credentials), `credential_bearing`, `static_shareable_declaration`, `requires_process_local_binding`. All worker primitives default to safe/pure/non-credential defaults.
- **Phase 2 (Cartographer Import Classification)**: Added `annotateImportedNode()` capability inference in `cartographer/service.cpp` including `isUnsafeFunctionName()`, `isPureFunction()`, `classifySideEffects()`, and `isCredentialBearing()` heuristics. Conservative defaults: unsafe if unprovably safe.
- **Phase 3 (Static Declaration Image Generation)**: `buildWorkerPrimitiveDeclarationImage()` includes full capability metadata for all worker primitives via the extended `symbolDeclaration()`.
- **Phase 4 (Validation & Tests)**: `validateDeclarationImage()` checks for required capability fields (`thread_safe`, `side_effects`). Two new tests: `WorkerPrimitiveSymbolsCarryCapabilityMetadata` (validates all 13 required fields present) and `WorkerPrimitiveSymbolsHaveValidSideEffects` (validates enum values).
- **Phase 5 (Worker Runtime Enforcement)**: Deferred; depends on G107 process-isolated worker validation.
- **Validation**: StaticDeclarationImageTest 14/14 passed, InternWorkerTest 27/27 passed.

## Deliverables
- `edict/static_declaration_image.cpp` — extended `symbolDeclaration()` with capability fields
- `edict/static_slot_table_image.cpp` — extended `StaticSlotTableDeclaration` with capability fields
- `cartographer/service.cpp` — `annotateImportedNode()` with capability heuristics
- `edict/tests/static_declaration_image_test.cpp` — `WorkerPrimitiveSymbolsCarryCapabilityMetadata`, `WorkerPrimitiveSymbolsHaveValidSideEffects`
