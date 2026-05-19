# Goal: G110 — Root1 eventfd/epoll Resource Broker and Micro-VM IPC Design

**Status**: ACTIVE
**Priority**: IMMEDIATE DESIGN / FIRST PROTOTYPE TRACK
**Created**: 2026-05-16
**Updated**: 2026-05-18
**Parent**: 🔗[G078 — Edict-Resident Agent Loop Consolidation](../G078-EdictResidentAgentLoopConsolidation/index.md)
**Immediate Consumer**: 🔗[G091 — Intern Worker Concurrency MVP](../G091-InternWorkerConcurrencyMvp/index.md)
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)
**Related Goals**: 🔗[G106 — Root1 Slab Advertisement Registry](../G106-Root1SlabAdvertisementRegistry/index.md), 🔗[G107 — Process-Isolated Micro-VM Interns](../G107-ProcessIsolatedMicroVmInterns/index.md)

## Objective
Design Root1 as an `eventfd`/`epoll`-backed **resource broker** for Edict micro-VMs: a centralized slab-directory, mailbox, waitable, and ownership-signalling hub that lets threads/processes coordinate around mutable shared coordination slabs without assigning one file descriptor per slab item.

The first concrete use case is high-performance async micro-VM IPC:

- shared mutable mailbox/control slabs hold descriptors and payload handles;
- each participant owns one eventfd or a small fixed set of eventfds;
- Root1 multiplexes participant fds, timers, sockets, process-death fds, and provider/event streams through `epoll`;
- Root1 keeps wait queues keyed by slab/resource ids;
- zero-copy payloads are represented by published slab handles rather than copied JSON where safe.

The longer-term use case is a futex/parking-lot-style ownership protocol for mutable shared resources: atomic state words or sidecar entries on resources provide the uncontended fast path, and Root1 wakes contended waiters in order through participant eventfds.

## Superseded Ideas

This goal supersedes the narrower initial framing of G110 as only an eventfd mailbox design.

Current best framing:

```text
per-participant eventfd(s)
+ Root1 epoll loop
+ Root1 wait queues keyed by ResourceKey
+ atomic resource state words/sidecars for fast-path ownership
+ mailbox descriptors for grants/events/payload handles
```

Superseded/deprioritized ideas:

- **One eventfd per slab item** — rejected as fd-heavy and unnecessary.
- **Mailbox-only design** — too narrow; mailboxes are the first application of the Root1 resource broker.
- **Pure ad-hoc ghost queue as long-term intern IPC** — still acceptable as a short fallback, but the target async substrate is broker-compatible waitables.
- **Arbitrary shared mutable Listree as the first target** — deferred; start with explicit coordination cells, mailboxes, resource descriptors, and coarse resource/subtree ownership.

## Rationale

`eventfd` and `epoll` fit the AgentC/Edict architecture because they separate **data location** from **readiness notification**:

- shared/mmap slabs can hold durable descriptors, resource states, payload handles, epochs, and mailbox records;
- kernel fd state stays process-local and is rebuilt on resume;
- `eventfd` wakes participants without making fds part of every resource;
- `epoll` lets Root1 multiplex all waitable events in one scheduler;
- Edict async/await can be modeled as parked VM continuations resumed by logical waitables;
- Root1 can unify thread workers, process-isolated workers, provider streams, cancellation, timers, and ownership grants.

The design is analogous to Linux futexes or userspace parking-lot locks:

1. try the uncontended operation in shared memory with atomics;
2. on contention, register with the broker and park;
3. Root1 wakes the next waiter by writing to that participant's eventfd;
4. the participant drains its mailbox/grant descriptor and resumes.

## Candidate Resource Model

### Resource key

A waitable/ownable resource should be named by a stable logical key rather than by a raw pointer:

```cpp
struct ResourceKey {
    uint32_t layer_id;
    uint32_t slab_id;
    uint32_t offset;
    uint16_t allocator_kind; // ListreeValue, ListreeItem, mailbox slot, descriptor, etc.
    uint16_t field_id;       // optional subresource/field selector
    uint64_t generation;     // ABA/stale-slot protection
};
```

The `generation` field is important: a slab slot can be freed/reused, and waiters for the old object must not receive grants for a new object occupying the same `(slab, offset)`.

### Resource state

Do not overload current `LtvFlags` as interprocess synchronization bits in the first design. Instead use either:

- dedicated atomic state words in explicit mutable coordination/mailbox slabs; or
- Root1/process-local sidecar tables keyed by `ResourceKey` for existing Listree values.

Candidate state bits/fields:

```text
UNOWNED
OWNED(owner_participant, owner_epoch)
CONTENDED / OWNERSHIP_REQUESTED
GRANTED(grant_token, waiter_participant)
POISONED / OWNER_DIED
```

The exact packing can be refined, but the protocol needs owner identity, generation/epoch protection, and an explicit contended/requested bit.

### Participant model

Each participant should have:

- logical participant id;
- process/thread/vm identity;
- one eventfd or a small fixed eventfd set;
- participant mailbox for grant/event descriptors;
- optional pidfd/process-death registration for process-isolated workers;
- VM continuation table for future `await!` resumption.

Root1 stores:

```text
participant_id -> eventfd + mailbox metadata
ResourceKey -> waiter queue / grant state / lease metadata
```

## Ownership Protocol Sketch

### Acquire fast path

```text
CAS(resource.state, UNOWNED -> OWNED(me, epoch))
```

If this succeeds, no syscall and no Root1 involvement are needed.

### Acquire slow path

```text
set CONTENDED / OWNERSHIP_REQUESTED bit
enqueue wait request with Root1
recheck resource state to avoid lost wake
park current thread/process/continuation on participant eventfd or scheduler waitable
```

The recheck is mandatory: the owner may release the resource between the failed CAS and waiter registration.

### Release path

Two release modes should be evaluated:

1. **Barging-tolerant release**: owner release-stores `UNOWNED`; if contended, notifies Root1. Simple, but a new contender can acquire before an older queued waiter.
2. **Broker-granted release**: owner releases to Root1; Root1 selects the next waiter, records a grant token, wakes that waiter, and the waiter claims ownership with the token. More machinery, but supports fairness and ordered wakeups.

The broker-granted mode is probably the better semantic fit for AgentC because Root1 can enforce queue order, priorities, cancellation, and owner-death recovery.

## First Concrete Use: Mailbox/Async IPC

The first implementation should target explicit coordination slabs, not arbitrary Listree mutation.

Mailbox path:

1. writer reserves a ring slot or appends an event record;
2. writer stores a descriptor referencing inline data, short JSON, a blob, or a published slab root;
3. writer release-publishes the descriptor;
4. writer writes the target participant/broker eventfd;
5. Root1 or the participant wakes from `epoll_wait(...)`;
6. reader drains the eventfd counter, acquire-loads queue state, and drains descriptors.

This maps naturally to:

```edict
task intern_start! @job
job intern_sync! @status
job await! @result      -- future continuation-parking surface
mailbox send! @event    -- later explicit mailbox surface
mailbox recv! @event
```

## Persistence Boundary

Raw file descriptors, eventfd counters, epoll registrations, pidfds, and kernel wait queues are not durable session state.

Persistent or mmap-resumable state should store only:

- logical participant/mailbox ids;
- mailbox slab/layer ids;
- resource keys;
- resource generation/epoch;
- descriptor ring head/tail/epoch;
- pending event descriptors;
- owner identity as a lease/epoch, not durable pid authority;
- enough metadata for Root1 to reconstruct waitables and recover abandoned resources.

On resume, Root1 recreates eventfds, rebuilds the epoll set, rescans mailbox/resource descriptors, and resolves stale owners/waiters according to lease/epoch policy.

## Implementation Plan

- [x] Write the Root1 resource-broker design note: resource keys, participant ids, state words, wait queues, grant tokens, lifecycle, persistence, and owner-death semantics.
- [x] Define first fixed-size mailbox descriptor and bounded ring layout compatible with future mmap coordination slabs.
- [x] Define the first mutable coordination slab file/mapping layout for participant records, resource state words, and ring placement.
- [x] Define the participant eventfd/mailbox model and Root1 epoll loop responsibilities.
- [x] Specify acquire/release memory ordering and the lost-wake avoidance protocol.
- [x] Decide whether the first ownership grant semantics allow barging or require broker-granted fairness; first prototype uses broker-granted transfer.
- [x] Reconstruct participant eventfds/epoll registrations from mapped participant mailbox records after remap/resume.
- [x] Add first cancellation/backpressure descriptor states and broker send helpers.
- [x] Add first pidfd/process-death descriptor path for process-isolated workers; broader abandoned-resource recovery policy remains future work.
- [x] Prototype a tiny Linux C++ broker smoke test outside VM dispatch: two participants contend for one logical resource and Root1 wakes waiters in order.
- [x] Add a descriptor-drain test for participant mailbox events through eventfd/epoll.
- [x] Extend the prototype to a bounded mmap-compatible mailbox ring.
- [x] Decide whether G091 async interns adopt the broker immediately or land a minimal broker-compatible in-process backend first: implemented Edict-local `InternJobManager` backed by G110 broker descriptors and waitable-shaped envelopes.
- [x] Add first non-parking generic `root1.await!` over logical Root1 waitables: poll the participant waitable, drain mailbox descriptors, and return a structured ready/timeout envelope. Continuation parking remains a later VM scheduler slice.
- [x] Add first abandoned-resource recovery helper: `Root1ResourceBroker::recoverAbandonedResource(...)` verifies the abandoned owner, clears unowned resources, or broker-grants ownership to the next queued valid waiter while publishing `OwnerDied` plus `OwnershipGranted` descriptors.
- [ ] Define how future Edict `await!` parks/resumes VM continuations through Root1 waitables.

## Acceptance Criteria

- [x] The design eliminates per-item eventfds while preserving waitability for arbitrary logical slab resources through Root1 wait queues.
- [x] The design has a concrete `ResourceKey`, resource state, participant, and grant-token model.
- [x] The design clearly separates persistent slab metadata from process-local/kernel fd state.
- [x] The design explains fast-path atomic acquire/release and slow-path Root1 parking/wakeup, including lost-wake prevention.
- [ ] The design covers mailbox IPC, async intern jobs, future `await!`, cancellation/backpressure, and owner-death recovery. Mailbox IPC, first async intern jobs, cancellation/backpressure descriptors, first pidfd owner-death descriptors, first abandoned-resource recovery helper, and first non-parking Edict `root1.await!` are prototyped; continuation-parking `await!` semantics remain.
- [x] A minimal Linux prototype demonstrates eventfd wakeup, epoll dispatch, contended resource grant, bounded mailbox ring behavior, and mailbox descriptor drain.

## First Prototype Slices — 2026-05-16

Implemented a small reusable Linux prototype in `core/root1_resource_broker.h/.cpp` and regression coverage in `tests/root1_resource_broker_tests.cpp`.

Prototype capabilities:

- `ResourceKey` identifies logical slab/resources by layer, slab, offset, allocator kind, field, and generation.
- `ResourceState` is an atomic ownership word with owner bits plus an ownership-requested/contended bit.
- `Root1ResourceBroker` registers participants with one eventfd each and adds them to a Root1-owned epoll set.
- Fast path: `tryAcquire(...)` CASes `UNOWNED -> owner` with no eventfd/syscall interaction.
- Slow path: `acquireOrQueue(...)` registers a waiter under Root1's mutex, sets the contended bit, and avoids the simple lost-wake race by retrying acquisition while serialized with broker release.
- Release path uses broker-granted transfer: Root1 selects the next waiter, stores ownership to that participant, emits an `OwnershipGranted` descriptor, and wakes the participant eventfd.
- `MailboxDescriptor` defines a fixed-size descriptor with event kind, payload kind, correlation id, grant token, `ResourceKey`, optional slab payload handle, and 96 inline bytes.
- `MailboxRing` defines a bounded 64-slot SPSC-style ring with release/acquire publication and no dynamic allocation in the ring layout.
- `CoordinationSlab` defines the first file-backed/anonymous mmap-compatible layout: header, 16 participant slots, participant mailbox rings, 64 resource slots, resource keys, and mapped `ResourceState` words.
- `registerParticipantOnSlab(...)` lets the broker write descriptors directly into a mapped participant mailbox while fd/eventfd state remains process-local.
- `reconstructParticipantsFromSlab(...)` and `reconstructParticipantFromSlab(...)` rebuild process-local eventfds/epoll registrations from mapped participant slots after remap/resume and can notify participants with pending mailbox descriptors.
- `sendCancellation(...)` and `sendBackpressure(...)` add first broker-level cancellation/backpressure descriptor states.
- `attachParticipantPid(...)` adds first pidfd-based owner-death monitoring; process exit is reported as an `OwnerDied` mailbox descriptor.
- G091 async interns now use an Edict-local `InternJobManager` backed by G110 `Root1ResourceBroker` descriptors and waitable-shaped job envelopes, including cooperative `Cancelled` and `Backpressure` descriptor policy.
- Mailbox path: `sendMailboxMessage(...)`, `sendMailboxDescriptor(...)`, `sendCancellation(...)`, `sendBackpressure(...)`, pidfd `OwnerDied` reports, `pollReadyParticipants(...)`, `drainMailbox(...)`, and `drainMailboxDescriptors(...)` demonstrate descriptor delivery through eventfd/epoll.

Prototype limits:

- Broker wait queues remain process-local sidecar maps; only participant mailbox rings and resource state slots have a first mapped layout.
- First abandoned-resource recovery exists for a known `ResourceKey`/`ResourceState`/owner tuple, but broader lease scanning, stale-owner detection, and process-worker policy remain future work.
- G091 now consumes cancellation/backpressure descriptor states for cooperative `intern_cancel!` and `max_active_jobs` backpressure; broader scheduler-level policy for process workers and continuation-parking `await!` remains.
- Generic module-backed `root1.await!` now exists as a non-parking helper over imported Root1 primitives; it polls a logical waitable and drains descriptors into a ready/timeout envelope. VM continuation parking/resume semantics remain future work.
- The mailbox ring is SPSC-style and broker-serialized in the current prototype; MPSC/per-producer lanes are still future work.

Validation:

- 2026-05-18: `cmake --build build --target reflect_tests edict_tests -j2` — passed.
- 2026-05-18: `./build/tests/reflect_tests --gtest_filter='Root1ResourceBrokerTest.*' --gtest_brief=1` — passed 12/12.
- 2026-05-18: `./build/edict/edict_tests --gtest_filter='Root1PrimitiveModuleTest.*' --gtest_brief=1` — passed 1/1.
- Earlier: `./build/tests/reflect_tests` — passed 43/43.
- Earlier: `cmake --build build --target cpp_agent_tests -j2` — passed.

## Progress Notes

### 2026-05-18
- Did: Added `Root1ResourceBroker::recoverAbandonedResource(...)` for known-resource abandoned-owner recovery, with tests for queued-waiter grant and no-waiter unowned recovery; added `agentc_root1_await_ltv` plus module-backed `root1.await!` that polls a logical waitable and drains descriptors into ready/timeout envelopes.
- Decided: First `root1.await!` is intentionally non-parking; it proves the logical waitable descriptor surface without introducing VM continuation scheduling yet.
- Remaining: Broader lease/stale-owner scanning, process-worker owner-death recovery policy, and true continuation-parking `await!` semantics.
- Next: Design the Root1 lease/stale-owner table shape that can detect abandoned owners without being handed a specific `ResourceKey`/owner tuple.

## Integration With Existing Goals

- 🔗[G091](../G091-InternWorkerConcurrencyMvp/index.md): the first async intern backend now uses an Edict-local `InternJobManager` with G110 broker-compatible waitables/descriptors, cooperative cancellation, and active-job backpressure; future process workers can move this onto mapped coordination slabs without changing the high-level envelope shape.
- 🔗[G111](../G111-Root1WorkerPrimitiveFfiEdictInternMigration/index.md): completed migration exposes Root1/waitable/mailbox and fresh-worker-VM capabilities as importable native primitives and rebuilds intern words as Edict module code, so G110 is reusable substrate rather than intern-specific VM dispatch.
- 🔗[G109](../G109-ListreeReadOnlyMutationSurfaceHardening/index.md): logical read-only safety remains necessary for shared context/imports; the broker handles mutable coordination resources, not arbitrary frozen-tree mutation.
- 🔗[G099](../G099-InternTaskQualityContracts/index.md): task/result contracts should include event kinds, progress, cancellation, timeout, backpressure, waitable ids, and ownership/error states if the broker path is adopted.
- 🔗[G105](../G105-ReadOnlyStaticSlabOwnershipModel/index.md): broker-managed mutable coordination slabs must be separate from read-only static/import/result slabs.
- 🔗[G106](../G106-Root1SlabAdvertisementRegistry/index.md): Root1's slab directory/publication registry should share identity, lease, and epoch concepts with the resource broker.
- 🔗[G107](../G107-ProcessIsolatedMicroVmInterns/index.md): process-isolated workers should use broker-managed eventfd/epoll mailboxes and resource grants for IPC.
- 🔗[G096](../G096-AuthoritativeMmapSessionResume/index.md): fds must be reconstructed on resume; durable state is descriptor/resource metadata plus lease epochs.

## Non-Goals For First Slice

- Do not expose raw Linux fds as ordinary Edict values.
- Do not create one eventfd per slab item.
- Do not overload current `LtvFlags` as cross-process synchronization words in the first design.
- Do not build arbitrary shared mutable Listree mutation first; start with explicit coordination cells, mailbox slabs, publication/resource descriptors, and coarse ownership.
- Do not persist fd numbers, eventfd counters, epoll registrations, or pid authority as authoritative state.
