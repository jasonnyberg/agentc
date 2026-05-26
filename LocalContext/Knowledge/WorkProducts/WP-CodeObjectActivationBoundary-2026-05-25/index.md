# WP — Code Object / Activation State Boundary

**Date**: 2026-05-25  
**Goal**: 🔗[G104 — Immutable Code Object / Activation Frame Split](../../Goals/G104-ImmutableCodeObjectActivationFrameSplit/index.md)  
**Related**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md), 🔗[WP — Listree Traversal State and ReadOnly Slab Audit](../WP-ListreeTraversalStateReadOnlySlabAudit-2026-05-14/index.md)

## Purpose

Document the current boundary between static-shareable compiled Edict code objects and private VM activation state after the first G104 implementation slice.

## Static-Shareable Code Object

A compiled Edict code object is a `ListreeValue` with binary payload bytes and a `.code_frame` marker. The binary payload is the bytecode image. It is descriptive program data and can be frozen/read-only or eventually placed in a static declaration/core image.

Static-shareable fields:

- Binary bytecode payload bytes.
- `.code_frame` marker identifying the value as dispatchable Edict bytecode.
- Future immutable descriptive metadata such as content hash, bytecode version, source provenance, or import/capability declaration references.

Static-shareable code objects must not contain live execution progress, scheduler identity, native handles, raw process pointers, or mutable counters.

## Activation-Local VM State

Execution progress belongs to the owning `EdictVM`, not to the code object. The current implementation stores live instruction pointers in `EdictVM::code_ips_`, aligned top-first with the `VMRES_CODE` stack. When a code object is enqueued as a frame, the VM creates a private activation IP entry initialized to 0. When the frame is popped, the matching IP entry is removed.

Activation-local state:

- Current instruction pointer for each live `VMRES_CODE` frame.
- `instruction_ptr`, `code_ptr`, and `code_size` scratch values used while dispatching the active frame.
- `tail_eval`, scan mode/depth, yield/error/complete flags, and transaction checkpoint copies of private code-stack/IP state.
- Any future continuation, scheduler, parking, lease, or owner metadata needed to resume a live activation.

This state is private dynamic VM state and is not static-shareable. Durable/session-safe continuation reconstruction remains a separate G096/G110/G104 integration problem, not a reason to write mutable progress back into static code objects.

## Legacy Compatibility

The VM still recognizes old/foreign binary frames carrying `.ip` as dispatchable code frames. `readFrameIp(...)` can rebuild from legacy `.ip`, and `writeFrameIp(...)` can update `.ip` only for non-read-only frames that are not present on the live `VMRES_CODE` stack. For live frames, `code_ips_` is authoritative.

This compatibility path exists for transition safety and should not be used by new code generation. New code frames should be produced through `makeCodeFrame(...)`, which emits `.code_frame` and no `.ip`.

## Freeze Semantics

`freeze!` / `ListreeValue::setReadOnly(true)` may now mark binary code objects read-only. This is safe because dispatch mutates VM-private activation state rather than the binary node. Frozen/shared thunks can be invoked, yield/resume, and be invoked again from IP 0 without mutating the shared code object.

## Invariants

- New compiled code frames use `.code_frame`, not `.ip`.
- `VMRES_CODE` and `EdictVM::code_ips_` stay aligned in top-first stack order.
- `writeFrameIp(...)` must update `code_ips_` for live frames and must not mutate read-only code objects.
- Transaction rollback that restores the code resource must also restore `code_ips_`.
- Reset/runtime preservation must rebuild private IP state from legacy frames when needed.
- Static-shareable code objects must remain free of raw fds, provider/runtime handles, scheduler pointers, or continuation-owner state.

## Validation

Implemented coverage includes:

- Frozen thunk yield/resume/reuse starts each fresh invocation from IP 0.
- `freeze!` marks binary thunks read-only.
- Recursive Listree freeze marks binary descendants read-only.
- Existing thunk/eval/yield focused tests continue to pass after the split.
