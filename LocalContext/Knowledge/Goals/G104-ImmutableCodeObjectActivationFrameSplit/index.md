# Goal: G104 — Immutable Code Object / Activation Frame Split

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G103 — Build-Time Static Core Declaration Image MVP](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Separate immutable Edict code/bytecode objects from mutable execution activation frames so compiled modules can eventually live safely in read-only static core slabs while each VM keeps instruction pointers and execution state in private dynamic slabs.

## Rationale
Static core slabs are only safe if their contents are truly immutable. Current code-frame behavior can associate mutable instruction-pointer state with binary/bytecode nodes, which is why recursive `ReadOnly` skips binary nodes. A static core image requires the normal runtime split between immutable code objects and private activation frames.

Related audit: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md) identifies `.ip` code-frame mutation as one of the slab-resident operational state writes that must move into VM-private activation state.

## Implementation Plan
- [ ] Audit current bytecode/thunk/code-frame mutation points.
- [ ] Define an immutable code object representation suitable for static slabs.
- [ ] Define private activation frames containing instruction pointer and execution-local metadata.
- [ ] Update thunk/eval execution to instantiate private frames from immutable code objects.
- [ ] Preserve existing Edict source behavior and stack semantics.
- [ ] Add regression coverage proving shared/frozen code objects can be executed without mutation.

## Acceptance Criteria
- [ ] Compiled code objects can be marked/read as immutable without breaking execution.
- [ ] Instruction pointer and frame metadata are stored only in private VM-owned dynamic state.
- [ ] Existing thunk/eval tests continue to pass.
- [ ] Documentation states which VM data is static-shareable and which is activation-local.

## Dependencies
- Supports 🔗[G103](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md) and later read-only static core images.
- Reduces risk for 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md) layered mmap mounts.
