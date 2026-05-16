# Goal: G092 — Cartographer FFI Re-entrancy Metadata

**Status**: PLANNED  
**Created**: 2026-05-14  
**Parent**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)

## Objective
Extend Cartographer/import metadata so AgentC can classify imported native symbols by re-entrancy and enforce which capabilities may be shared with worker VMs.

## Source Extracted From
Conversation summary section **“FFI re-entrancy classification”**.

## Rationale
Intern concurrency requires explicit capability safety. Pure computation and stateless I/O can be shared across worker sandboxes; stateful runtime/provider handles must be cloned, coordinator-owned, or blocked. Without metadata, worker import sharing is either too risky or too conservative.

## Proposed Classification
| FFI type | Re-entrant | Shareable | Example |
|----------|------------|-----------|---------|
| Pure computation | yes | yes | string/number transforms |
| Stateless I/O | yes | yes | JSON-envelope file read |
| Stateful handle | no | no | provider/runtime request handle |
| Shared-cell primitive | yes, if synchronized | yes, if contract-safe | explicit shared-cell API |

## Full-Send Loader Metadata
The layered mmap/meta-library concept makes G092 foundational for static declaration images. Imported symbols should carry richer metadata than only `reentrant` / `shareable`:

```json
{
  "static_shareable_declaration": "true",
  "requires_process_local_binding": "true",
  "thread_safe": "true",
  "process_safe": "true",
  "reentrant": "true",
  "pure": "false",
  "side_effects": ["filesystem"],
  "credential_bearing": "false",
  "worker_allowed": "true"
}
```

This metadata lets static core slabs share declarative library namespaces while each worker/coordinator process decides whether it may lazily bind and call the native capability.

## Implementation Plan
- [ ] Define native metadata fields for imported symbols: at minimum `reentrant`, `shareable`, and `stateful` or equivalent.
- [ ] Extend the field set for static declaration images: `static_shareable_declaration`, `requires_process_local_binding`, `thread_safe`, `process_safe`, `pure`, `side_effects`, `credential_bearing`, and `worker_allowed`.
- [ ] Add a conservative default policy for unannotated imports.
- [ ] Add a way for built-in/imported AgentC extension surfaces to declare classifications.
- [ ] Teach worker dispatch/import sharing to reject non-shareable symbols.
- [ ] Expose enough metadata to Edict for inspection/debugging.

## Acceptance Criteria
- [ ] Tests classify at least one pure/stateless function as shareable.
- [ ] Tests classify a stateful provider/runtime request path as non-shareable.
- [ ] Worker setup refuses or omits non-shareable imports.
- [ ] Error messages make the rejected symbol and classification clear.
- [ ] Documentation explains the default policy and how extensions should annotate functions.

## Dependencies
- Supports 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md), but can be prototyped independently on the import metadata path.
