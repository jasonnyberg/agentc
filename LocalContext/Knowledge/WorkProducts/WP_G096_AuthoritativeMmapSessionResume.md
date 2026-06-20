# WP — G096 Authoritative mmap Session Resume Boundary

**Goal**: G096 — Authoritative mmap Session Resume  
**Status**: Complete  
**Date**: 2026-06-20

## Summary

G096 moves AgentC's session resume path from a JSON/transcript rebuild model toward authoritative mmap/session-image ownership. The completed slice covers deterministic root/session restore, file-backed slab[0] persistence, scheduler continuation-table persistence, and layered static declaration-image mounts that survive session save/load.

## Durable State

The following state is now treated as durable at the session-image boundary:

- Session root anchor and Listree-backed root graph through `SessionStateStore` / `SessionImageStore` allocator images.
- File-backed allocator slab files, including slab[0], when the file-backed path is enabled.
- Root1 await scheduler continuation table via `Root1AwaitScheduler::saveState()` / `loadState()` and its session integration.
- Declarative runtime/import rehydration metadata stored on the agent root.
- Static mount paths in session bootstrap metadata (`static_mounts`) and restored static bases returned by `SessionStateStore::loadRoot(...)`.
- Read-only G103 static declaration images mounted through `readDeclarationImageContainerMmapReadOnly(...)` and `mountDeclarationImageReadOnly(...)`, with registry metadata describing root/section provenance.

## Transient / Rehydrated State

The following state is explicitly not persisted as raw authority:

- Raw file descriptors, eventfds, epoll instances, pidfds, sockets, and provider/runtime handles.
- Native `dlopen` / `dlsym` handles and raw function pointers.
- Process-local VM object pointers and native scheduler/broker instances.
- Provider credentials and live network/session connections.

Instead, these must be recreated from declarative metadata, broker resource keys, scheduler continuation metadata, static mount manifests, or provider/runtime bootstrap descriptors during process-local rehydration.

## Deferred Work

G096 deliberately stops short of full kill-mid-op activation-frame resurrection. The following remain future goals:

- Stable code-object identity and versioning across static image revisions.
- Full activation-frame serialization, including stack/code frame identity beyond the existing scheduler continuation table.
- G106 Root1 publication registry for immutable worker result/publication layers.
- Broader manifest compatibility policy across multiple static/session image versions.

## Verification Matrix

| Coverage | Test / Command | Evidence |
|---|---|---|
| Layered static mounts survive save/load | `./cpp-agent/cpp_agent_tests --gtest_filter='SessionStateStoreTest.LayeredStaticMountSurvivesSessionRoundTrip'` | Passed 1/1 on 2026-06-20 |
| Persistence + embedded VM restore affected slice | `./cpp-agent/cpp_agent_tests --gtest_filter='SessionStateStoreTest.*:EmbeddedVmRootRestoreTest.*'` | Passed 21/21 on 2026-06-20 |
| Full cpp-agent regression binary | `./cpp-agent/cpp_agent_tests` | Passed 53/53 on 2026-06-20 |

## Related Goals

- G103 — Build-Time Static Core Declaration Image MVP
- G104 — Immutable Code Object / Activation Frame Split
- G105 — ReadOnly Static Slab Ownership Model
- G106 — Root1 Slab Advertisement Registry
- G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design
