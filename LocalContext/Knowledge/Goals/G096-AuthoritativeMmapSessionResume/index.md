# Goal: G096 — Authoritative mmap Session Resume

**Status**: PLANNED — dependencies G104 (code/activation split), G105 (static slab model) complete; G110 scheduler persistence prerequisite landed 2026-06-06<br>
**Created**: 2026-05-14<br>
**Updated**: 2026-06-06<br>
**Depends On**: G104 (code/activation split — complete), G105 (static slab model — complete), G110 (broker fd reconstruction — prototype complete; scheduler persistence landed)

## Objective
Complete the persistence direction where AgentC's cognitive state is backed by mmap-owned slabs that can be flushed and restored with minimal reconstruction, enabling kill/restart/resume behavior for agent state.

## Source Extracted From
Conversation summary section **"Persistent cognitive state via mmap"**.

## Rationale
The architectural vision depends on persistent cognitive state being a substrate feature, not a serialization convention. Existing work products describe mmap-friendly slab images and restore paths, but the user-facing promise requires a hardened authoritative mmap ownership model and a clear recovery contract.

## Related Knowledge
- 🔗[WP — Session Image Persistence Plan](../../WorkProducts/WP_SessionImagePersistencePlan_2026-05-01.md)
- 🔗[WP — Embedded Persistent Agent Architecture](../../WorkProducts/WP_EmbeddedPersistentAgentArchitecture.md)

## Current Status — 2026-05-14
A prerequisite CLI/session namespace slice landed in 🔗[G102 — Edict Session ID Startup Flag](../G102-EdictSessionIdStartupFlag/index.md): raw Edict now supports `--session ID` / `--session-base DIR`, stores session images under `/tmp/session/<id>/` by default, and can persist/restore a non-trivial root binding across process invocations through the current `SessionStateStore`/`SessionImageStore` path. This is not full kill-mid-turn authoritative mmap resume, but it establishes the user-facing session selector and exercises the existing slab-image persistence boundary from the raw Edict executable.

## Full-Send Slab Direction
The layered mmap micro-VM concept sharpens G096 from flat session restore into **layered arena mounts**:

- build-time static core/declaration images mounted read-only;
- private writable VM/session overlays;
- Root1-brokered mutable coordination/mailbox slabs with durable descriptors but transient fd state;
- optional Root1-advertised read-only publication slabs;
- manifest/hash/version validation before attach;
- transient/native handles and eventfd/epoll waitable state rehydrated lazily and locally from declarative metadata, resource keys, or mailbox descriptors.

Near-term prerequisite goals now slice this work:
- 🔗[G103 — Build-Time Static Core Declaration Image MVP](../G103-BuildTimeStaticCoreDeclarationImageMvp/index.md)
- 🔗[G104 — Immutable Code Object / Activation Frame Split](../G104-ImmutableCodeObjectActivationFrameSplit/index.md)
- 🔗[G105 — ReadOnly Static Slab Ownership Model](../G105-ReadOnlyStaticSlabOwnershipModel/index.md)
- 🔗[G106 — Root1 Slab Advertisement Registry](../G106-Root1SlabAdvertisementRegistry/index.md)
- 🔗[G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design](../G110-EventfdEpollMicroVmIpcDesign/index.md)
- 🔗[G107 — Process-Isolated Micro-VM Interns](../G107-ProcessIsolatedMicroVmInterns/index.md)

## Implementation Plan
- [ ] Reconcile current session-image restore with the target authoritative mmap-backed slab ownership model.
- [ ] Define the allocator/runtime contract for file-backed slabs created from birth.
- [ ] Define layered arena mount semantics: static core image, private session overlay, optional published read-only layers.
- [ ] Define `msync`/checkpoint behavior and failure semantics.
- [ ] Ensure transient runtime/import/provider state is explicitly rehydrated after logical state attach.
- [ ] Add manifest/hash/version validation for mounted images.
- [ ] Add a deterministic resume test that persists non-trivial Edict/Listree state, tears down the VM/process boundary as much as practical, and restores it.

## Acceptance Criteria
- [ ] A documented session state can be restored without rebuilding the logical object graph from JSON/transcript text.
- [ ] The restore path handles Edict root/conversation/task state and rejects or rehydrates transient native handles safely.
- [ ] Regression coverage protects the mmap/session-image contract.
- [ ] User-facing docs state exactly what is durable, what is transient, and what remains future work.

## Notes
This is a foundational long-term goal. It should be sliced conservatively because it affects allocator, persistence, VM, and provider lifecycle boundaries.
