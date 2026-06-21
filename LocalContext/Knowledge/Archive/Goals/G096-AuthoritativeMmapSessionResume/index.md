# Goal: G096 — Authoritative mmap Session Resume

**Status**: COMPLETE — deterministic session restore, file-backed slab[0] persistence, scheduler continuation persistence, and layered static mount restore are verified<br>
**Created**: 2026-05-14<br>
**Updated**: 2026-06-20<br>
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
- 🔗[WP — G096 Authoritative mmap Session Resume Boundary](../../WorkProducts/WP_G096_AuthoritativeMmapSessionResume.md)

## Summary — Completed 2026-06-20
G096 is complete as the first authoritative mmap/session-resume milestone. It now has:

- deterministic session root restore through `SessionStateStore` / `SessionImageStore` allocator images;
- explicit bootstrap metadata authority, including preserved `static_mounts` paths;
- file-backed allocator support that preserves slab[0] roots across save/load;
- Root1 await scheduler continuation-table persistence via `saveState()` / `loadState()`;
- layered static declaration-image mount restore: session roots reload with `staticBases`, and `EdictVM(restoredRoot, staticBases)` receives the static layer below the private session overlay;
- a durable/transient/deferred boundary captured in the G096 WorkProduct.

The goal does **not** claim full kill-mid-op activation-frame resurrection. Stable code-object identity across image versions and full activation-frame serialization are explicitly deferred; the session boundary now safely persists logical root/scheduler/static-mount state while rehydrating or excluding process-local handles.

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
- [x] Reconcile current session-image restore with the target authoritative mmap-backed slab ownership model.
- [x] Define the allocator/runtime contract for file-backed slabs created from birth.
- [x] Define layered arena mount semantics: static core image, private session overlay, optional published read-only layers.
- [x] Define `msync`/checkpoint behavior and failure semantics.
- [x] Ensure transient runtime/import/provider state is explicitly rehydrated after logical state attach.
- [x] Add manifest/hash/version validation for mounted images.
- [x] Add a deterministic resume test that persists non-trivial Edict/Listree state, tears down the VM/process boundary as much as practical, and restores it.

## Acceptance Criteria
- [x] A documented session state can be restored without rebuilding the logical object graph from JSON/transcript text.
- [x] The restore path handles Edict root/conversation/task state and rejects or rehydrates transient native handles safely.
- [x] Regression coverage protects the mmap/session-image contract.
- [x] User-facing docs state exactly what is durable, what is transient, and what remains future work.

## Verification — 2026-06-20
- `cmake --build build --target cpp_agent_tests -j$(nproc)` passed.
- `./cpp-agent/cpp_agent_tests --gtest_filter='SessionStateStoreTest.LayeredStaticMountSurvivesSessionRoundTrip'` passed 1/1.
- `./cpp-agent/cpp_agent_tests --gtest_filter='SessionStateStoreTest.*:EmbeddedVmRootRestoreTest.*'` passed 21/21.
- Full `./cpp-agent/cpp_agent_tests` passed 53/53.

## Notes
This goal is the first authoritative mmap/session-resume milestone. Full activation-frame resurrection and Root1-published immutable result/publication layers remain future work, principally under G106 and later static-image/versioning follow-ups.
