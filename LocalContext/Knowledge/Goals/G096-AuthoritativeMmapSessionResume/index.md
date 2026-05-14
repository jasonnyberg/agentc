# Goal: G096 — Authoritative mmap Session Resume

**Status**: PLANNED  
**Created**: 2026-05-14

## Objective
Complete the persistence direction where AgentC's cognitive state is backed by mmap-owned slabs that can be flushed and restored with minimal reconstruction, enabling kill/restart/resume behavior for agent state.

## Source Extracted From
Conversation summary section **“Persistent cognitive state via mmap”**.

## Rationale
The architectural vision depends on persistent cognitive state being a substrate feature, not a serialization convention. Existing work products describe mmap-friendly slab images and restore paths, but the user-facing promise requires a hardened authoritative mmap ownership model and a clear recovery contract.

## Related Knowledge
- 🔗[WP — Session Image Persistence Plan](../../WorkProducts/WP_SessionImagePersistencePlan_2026-05-01.md)
- 🔗[WP — Embedded Persistent Agent Architecture](../../WorkProducts/WP_EmbeddedPersistentAgentArchitecture.md)

## Current Status — 2026-05-14
A prerequisite CLI/session namespace slice landed in 🔗[G102 — Edict Session ID Startup Flag](../G102-EdictSessionIdStartupFlag/index.md): raw Edict now supports `--session ID` / `--session-base DIR`, stores session images under `/tmp/session/<id>/` by default, and can persist/restore a non-trivial root binding across process invocations through the current `SessionStateStore`/`SessionImageStore` path. This is not full kill-mid-turn authoritative mmap resume, but it establishes the user-facing session selector and exercises the existing slab-image persistence boundary from the raw Edict executable.

## Implementation Plan
- [ ] Reconcile current session-image restore with the target authoritative mmap-backed slab ownership model.
- [ ] Define the allocator/runtime contract for file-backed slabs created from birth.
- [ ] Define `msync`/checkpoint behavior and failure semantics.
- [ ] Ensure transient runtime/import/provider state is explicitly rehydrated after logical state attach.
- [ ] Add a deterministic resume test that persists non-trivial Edict/Listree state, tears down the VM/process boundary as much as practical, and restores it.

## Acceptance Criteria
- [ ] A documented session state can be restored without rebuilding the logical object graph from JSON/transcript text.
- [ ] The restore path handles Edict root/conversation/task state and rejects or rehydrates transient native handles safely.
- [ ] Regression coverage protects the mmap/session-image contract.
- [ ] User-facing docs state exactly what is durable, what is transient, and what remains future work.

## Notes
This is a foundational long-term goal. It should be sliced conservatively because it affects allocator, persistence, VM, and provider lifecycle boundaries.
