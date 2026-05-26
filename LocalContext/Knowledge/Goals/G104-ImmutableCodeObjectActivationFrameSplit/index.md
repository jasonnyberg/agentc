# Goal: G104 — Immutable Code Object / Activation Frame Split

**Status**: ACTIVE / BOUNDARY DOCUMENTED
**Created**: 2026-05-14  
**Parent**: 🔗[G103 — Build-Time Static Core Declaration Image MVP](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Separate immutable Edict code/bytecode objects from mutable execution activation frames so compiled modules can eventually live safely in read-only static core slabs while each VM keeps instruction pointers and execution state in private dynamic slabs.

## Rationale
Static core slabs are only safe if their contents are truly immutable. Current code-frame behavior can associate mutable instruction-pointer state with binary/bytecode nodes, which is why recursive `ReadOnly` skips binary nodes. A static core image requires the normal runtime split between immutable code objects and private activation frames.

Related audit: 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../../WorkProducts/WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md) identifies `.ip` code-frame mutation as one of the slab-resident operational state writes that must move into VM-private activation state.

## Implementation Plan
- [x] Audit current bytecode/thunk/code-frame mutation points.
- [x] Define an immutable code object representation suitable for static slabs.
- [x] Define private activation frames containing instruction pointer and execution-local metadata.
- [x] Update thunk/eval execution to instantiate private frames from immutable code objects.
- [x] Preserve existing Edict source behavior and stack semantics.
- [x] Add regression coverage proving shared/frozen code objects can be executed without mutation.

## Acceptance Criteria
- [x] Compiled code objects can be marked/read as immutable without breaking execution.
- [x] Instruction pointer and frame metadata are stored only in private VM-owned dynamic state.
- [x] Existing thunk/eval tests continue to pass.
- [x] Documentation states which VM data is static-shareable and which is activation-local.

## Progress
- 2026-05-25: Landed the first VM implementation slice. `makeCodeFrame(...)` now creates binary code objects marked with `.code_frame` instead of writing mutable `.ip` fields; live instruction pointers are tracked in `EdictVM::code_ips_` aligned top-first with `VMRES_CODE`. Enqueue/dequeue, transaction checkpoint/rollback, runtime reset, thunk dispatch, and read/write frame IP paths now keep activation-local IP state in private VM memory while retaining legacy `.ip` fallback for old/foreign non-read-only frames.
- 2026-05-25: Added regression coverage proving a frozen thunk can yield/resume and then be reused from IP 0 without mutating the frozen code object. Also fixed a speculative nested-execution cleanup edge where failed speculation could leave a stale nested code frame on the live code stack.
- 2026-05-25: Validation passed: `cmake --build build --target edict_tests -j2`; focused frame tests `EdictVM.YieldedExecutionCanResumeCurrentCodeFrame:EdictVM.FrozenThunkUsesPrivateInstructionPointerAcrossResumeAndReuse:EdictVM.NestedEvalUsesCodeStack` 3/3; broader focused VM regression `EdictVM.*:FreezeBuiltin.*:SharingTest.*:VMStackTest.*:SimpleAssignTest.*:RegressionMatrixTest.*` 48/48; intern/root1 focused slice `InternWorkerTest.*:Root1PrimitiveModuleTest.*:Root1AwaitSchedulerTest.*` 20/20.
- 2026-05-25: Completed the explicit boundary documentation in 🔗[WP — Code Object / Activation State Boundary](../../WorkProducts/WP-CodeObjectActivationBoundary-2026-05-25/index.md), updated the stale read-only slab audit wording, and tightened freeze semantics so recursive read-only marking now includes binary thunk/code objects.

## Dependencies
- Supports 🔗[G103](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md) and later read-only static core images.
- Reduces risk for 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md) layered mmap mounts.
