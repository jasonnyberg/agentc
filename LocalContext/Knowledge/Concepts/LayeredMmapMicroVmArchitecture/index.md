# Layered mmap Micro-VM Architecture

## Scope

- Scope: LOCAL
- Granularity: Layer 2 concept
- Status: Active
- Created: 2026-05-14
- Last Referenced: 2026-05-14

## Summary

AgentC worker and intern VMs should eventually be tiny execution engines mounted over layered mmap-backed Listree/slab arenas: an immutable shared core image underneath private per-VM dynamic slabs, plus optional published communication slabs for zero-copy immutable result sharing.

The concept is to avoid copying the world into every worker. A bootstrap/core VM prepares static import/module slabs once; coordinator and intern VMs then mmap those slabs read-only and allocate only their own dynamic task/workspace/result state.

## Bigger Picture

This concept extends the G091 intern-worker direction and the G096 mmap-resume direction:

- 🔗[G091 — Intern Worker Concurrency MVP](../../Goals/G091-InternWorkerConcurrencyMvp/index.md) needs cheap worker VMs and safe shared context/imports.
- 🔗[G096 — Authoritative mmap Session Resume](../../Goals/G096-AuthoritativeMmapSessionResume/index.md) needs a durable, authoritative slab mapping model.
- 🔗[K030 — Arena Persistence Boundary](../../Facts/J3_AgentC/K030_J3_Arena_Persistence.md) and 🔗[ListreeValue Serialization Format](../../Facts/ListreeValueSerializationFormat.md) already establish slab image persistence and restore invariants.
- 🔗[K032 — Edict Intern Worker Surface](../../Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md) records the current `intern_run!` slice and the planned async `intern_start!` / `intern_sync!` path.

## Core Idea

A VM address space should be layered:

```text
private dynamic VM layer
    writable by one active VM
    contains stacks, task root, workspace, transient execution frames, result construction

published communication layer (optional)
    owner-writable before publication
    read-only to other VMs after publication
    contains immutable result subtrees, mailboxes, or group-shared snapshots

static core slab layer
    built once by a bootstrap/core VM
    mmap'ed read-only by all worker/coordinator VMs
    contains import dictionaries, module definitions, compiled static thunks, schemas, contracts
```

Lookup can conceptually walk from private to group to core:

```text
private overlay scope → group published scope → static core import/module scope
```

Writes go only to the private layer or to an explicitly owned communication slab. Shared/core layers are immutable to readers.

## Bootstrap Refinement: Build-Time Root0 to Runtime Root1

Nearly all of the system can eventually be turned into read-only slabs. The irreducible native bootstrap core is not a full always-resident VM; it is closer to a **slab loader plus Listree rehydrator**.

Root0 does not need to launch at every Edict session. It can be a build-time/static-image generation phase:

1. A build-time **Root0 image builder** links the current core normally.
2. The builder imports/constructs the static system surface: VM definitions, module dictionaries, import metadata, contracts, scaffolds, and bootstrap thunks.
3. The builder freezes and saves these as versioned core slab files plus a manifest.
4. The generated static core image is carefully versioned and validated as a build artifact.
5. Runtime Edict sessions skip core slab generation entirely.
6. A session starts by launching **Root1**, a micro-VM that mmap-installs the prebuilt static core slabs, rehydrates Listree roots, and mounts higher-level behavior from static imports.

In this target shape, the native micro-VM engine is tiny. It needs enough machinery to:

- mmap/install slab files,
- rehydrate Listree roots,
- resolve layered `SlabId`s,
- run the minimal dispatch/evaluation kernel,
- call into imported static/native capabilities.

Most higher-level behavior can live in the static core/import slabs rather than being linked directly into every worker process.

## Meta-Library Dynamic Loader

The static core image points toward a meta-library dynamic loader model. Instead of dynamically binding a library while bootstrapping each VM, the build-time image declares the library's contents as read-only Listree metadata:

- exported namespace shape,
- function/type signatures,
- Edict wrapper thunks,
- safety and re-entrancy metadata,
- required native library identity/path/hash,
- expected symbol names and ABI details,
- version/compatibility constraints.

This is a **declarative import image**, not a process-local dynamic binding. It lets a micro-VM mount the library namespace without immediately calling `dlopen`/`dlsym` or storing raw native handles in shared slabs.

Actual native binding can then be lazy and per-process:

```text
static slabs: declarative library namespace and call metadata
process-local native sidecar: dlopen/dlsym handles, closure pointers, provider/runtime handles
call boundary: resolve/revalidate metadata, then execute native capability
```

This preserves the memory benefit of shared static import slabs while keeping volatile process-local handles out of the read-only core image.

## Root1 and Inter-VM Messaging Tree

Root1 can own the inter-VM messaging/control tree. Tertiary micro-VMs do not need to own the global coordination structure. Instead:

- Root1 allocates or records handles for worker-owned slab publications.
- Each tertiary micro-VM owns its private dynamic slabs and optional writable publication slabs.
- When a worker publishes a result/mailbox subtree, Root1 advertises the slab manifest/root handle to other VMs.
- Other VMs mmap the advertised slabs read-only.
- Root1 remains the coordinator that decides which published roots become part of global state.

This requires a slab-advertisement mechanism: workers must be able to announce slab files, slab id ranges, root ids, epochs, permissions, and ownership to Root1 without requiring mutable shared Listree access.

## Why This Fits AgentC

AgentC already treats state as Listree values in slab arenas. That makes mmap-backed sharing more natural than in object-heap runtimes:

- Static module/import dictionaries can be persisted as slab images.
- The OS can share read-only mapped pages across many worker processes.
- Worker VMs can start with tiny private overlays rather than duplicating bootstrap/module graphs.
- Results can eventually be published as immutable slab roots instead of copied through JSON.
- Process isolation can enforce read-only core mappings at the OS level, not just by Listree flags.

## Candidate Layer Contents

### Static Core Layer

Good candidates:

- curated Edict module dictionaries
- precompiled module bytecode blobs
- Cartographer import metadata
- FFI function schemas and safety/re-entrancy declarations
- standard agent/intern task scaffolds
- stable policy/schema objects
- language/runtime contracts

Bad candidates:

- process-local raw handles
- provider/runtime handles
- `dlopen` handles
- raw function pointers as durable authority
- mutable instruction pointers or stack frames
- live sockets, streams, or credentials

### Per-VM Dynamic Layer

Good candidates:

- VM data/dict/function/code/resource stacks
- task envelope root
- private `workspace`
- transient execution frames
- local overrides/shadows of core bindings
- worker result construction before publication

### Published Communication Layer

Good candidates:

- immutable worker result roots
- append-only event batches
- published context snapshots
- group-level facts or summaries after owner freeze

The safe first communication rule should be immutable-after-publication, not arbitrary shared mutation.

## Key Design Requirements

### 1. Layered Slab Addressing

Current `SlabId` is effectively `(slab_index, offset)`. A layered mmap design needs a way to disambiguate slabs across layers, such as:

- reserved slab-index ranges per layer,
- an arena/layer id in the pointer resolution context,
- a per-VM layered arena table, or
- an extended pointer representation.

A mature design probably needs a VM-local `LayeredArenaContext` so slab ids can resolve through mounted layers without forcing every VM to own every slab.

A practical early allocation strategy could partition the slab id range by direction:

```text
core/static slabs:      allocate from slab id 0 upward
micro-VM dynamic slabs: allocate from the high end downward
```

That preserves compact low ids for the root static image while reducing collision risk for independent micro-VM allocations. A synchronized slab-id allocator or Root1-mediated range lease would still be needed once multiple processes publish slabs to each other.

### 2. Slab Advertisement and Range Leasing

For zero-copy inter-VM sharing, a worker must advertise enough metadata for another VM to mmap its published slabs safely:

- owner VM/process id
- slab id range or layer id
- slab file paths/content hashes
- root `SlabId`/published handle
- read/write permissions
- publication epoch/version
- lifecycle/garbage-collection policy
- whether the publication is immutable/final or still owner-private

Root1 is the natural place to own this registry. It can lease dynamic slab ranges, prevent id collisions, and decide when an advertised publication becomes visible to other VMs.

### 3. Immutable Bytecode vs Mutable Activation Frames

Current binary/bytecode nodes are skipped by recursive `ReadOnly` because execution state can write instruction-pointer metadata into code-frame nodes. Static core bytecode sharing needs a split:

```text
immutable bytecode blob in core slabs
mutable activation frame / ip state in private VM slabs
```

Without this split, readonly mapped module bytecode will be fragile.

### 4. Declarative Imports vs Native Handles

Static core slabs should store declarative import metadata, not process-local native handles as durable authority. Native handles should be rehydrated per process/VM from metadata.

This matches the G102 session lesson: volatile VM-owned/native bootstrap names need fresh installation instead of restoring stale binary/null history.

### 5. Overlay Shadowing and Tombstones

If a name exists in readonly core, `/name` cannot literally remove it from core. The dynamic overlay needs explicit semantics for local hiding/removal:

- tombstones,
- shadow-null entries,
- lexical masking,
- or “remove only affects the top writable layer.”

### 6. Process Isolation for Strong ReadOnly Enforcement

With separate worker processes, core slabs can be mmap'ed read-only per process. That gives stronger safety than same-process threads, because same-process native code can bypass Listree-level guards.

### 7. ReadOnly Slab Ownership: Refcounts and Pinning

Current `CPtr` and allocator behavior mutates allocator refcounts (`inUse`) when values are retained/released. `ListreeValue` also has mutable `pinnedCount` for cursor/path safety. A truly read-only mmap'ed slab cannot support those writes directly.

Candidate models:

- **Immortal static slabs**: core image nodes are permanently live; retain/release does not mutate their slab metadata. This is likely the best first model for static core/import slabs.
- **Process-local shadow refcount/pin tables**: read-only slab data stays immutable while each process tracks local ownership/pinning sidecars.
- **Borrowed/static handle APIs**: static values are explicit borrowed handles rather than normal owning `CPtr`s; this is clean but would infect many APIs.

Preferred first direction: treat core/static slabs and immutable published slabs as immortal for their lease lifetime. Use Root1/image manifests for lifecycle rather than node-local refcounting. Consider shadow refcounts only if later use cases require fine-grained local ownership accounting.

### 8. Versioned and Content-Addressed Core Images

Static core slabs should be reproducible and manifest-driven. A core image manifest should probably include:

```json
{
  "agentc_core_image_version": "...",
  "compiler_version": "...",
  "edict_language_version": "...",
  "listree_serialization_version": "...",
  "slab_format_version": "...",
  "endianness": "little",
  "word_size": 64,
  "source_hashes": {},
  "native_library_declarations": {},
  "slab_files": [
    {"path": "slab.0000.bin", "sha256": "...", "slab_range": "..."}
  ],
  "root_ids": {}
}
```

Generated core images should ideally be content-addressed, for example `core-image/sha256-.../manifest.json`, so many sessions/processes can mount exactly the same image with no ambiguity.

Version-control policy remains open. A conservative default is to version-control source manifests/generators, verify generated image hashes in CI, and publish full generated slab images as release artifacts. Small canonical test images may be committed for regression coverage.

### 9. Capability Metadata as Loader Contract

The meta-library loader makes G092 metadata central. Static slabs should classify capabilities more richly than just safe/unsafe:

```text
static-shareable-declaration: true/false
requires-process-local-binding: true/false
thread-safe: true/false
process-safe: true/false
reentrant: true/false
pure: true/false
side_effects: none/filesystem/network/process/credentials
credential-bearing: true/false
worker-allowed: true/false
```

This metadata lets the static image share declarations while each process decides whether it may lazily bind and call the native capability.

### 10. Hybrid Control/Event vs Bulk Slab Communication

Do not force all intern communication through shared slabs immediately. Use two paths:

- **Small/control data**: ghost-queue style JSON/event envelopes for running/complete/error/progress/small summaries.
- **Large/bulk data**: published read-only slabs for large result trees, ASTs, context summaries, search results, logs, or knowledge subgraphs.

This keeps the async intern path simple while preserving a zero-copy path for data large enough to justify the extra slab-publication machinery.

### 11. Forkserver as Intermediate Process Model

Before fully independent micro-process slab mounting, Root1 could act as a forkserver:

1. Root1 maps the static core image.
2. Root1 initializes only safe process-local sidecars.
3. Root1 forks worker processes that inherit mappings copy-on-write.

This could give fast launch and OS-level page sharing before the full slab-advertisement model is complete. Risks: inherited native handles must be sanitized, forking a multithreaded process is dangerous, and provider/runtime handles should not be inherited freely.

## Relationship to Async Intern Jobs

The async `intern_start!` / `intern_sync!` path is the control-plane mechanism. Layered mmap micro-VMs are the memory-substrate optimization.

A staged path:

1. `intern_run!` — synchronous deterministic worker execution with JSON result copy. **Current first slice.**
2. `intern_start!` / `intern_sync!` — async launch/poll using ghost-queue mechanics. **Primary next G091 path.**
3. Static declaration image prototype — build/mount a tiny read-only slab image containing declarative metadata for one module/library, with no native handles.
4. Immutable code object split — separate immutable bytecode blobs from mutable private activation frames.
5. Build-time Root0 core image — generate reproducible static core/import slabs and a manifest; runtime sessions launch Root1 directly.
6. Read-only core mounting — worker/coordinator VMs map imports/modules from the core image instead of copying bootstrap graphs.
7. Root1 slab advertisement — Root1 leases slab ranges and records worker-published immutable roots.
8. Published result slabs — workers publish immutable result roots; coordinator imports/merges by reference or copies on write.
9. Process-isolated micro-VM cores — many tiny intern processes map the same core image and own only private overlays or leased publication slabs.

## Risks and Open Questions

- How should `SlabId` encode or resolve layer identity?
- How should read-only static slabs interact with current mutable allocator refcounts and `ListreeValue::pinnedCount`?
- Should the first implementation use immortal static slabs, shadow refcount/pin sidecars, or a borrowed/static handle model?
- What is the exact Root0 build artifact: raw slab files, manifest, bootstrap root id, import registry, content hashes, or all of these?
- Should core slabs be built by raw `edict`, by a special bootstrap binary, by a build-system generator, or by a coordinator daemon?
- How much VM bootstrap can be moved out of C++ and into static imported/core slabs?
- What minimum native VM engine remains after import/module offloading?
- What should be version-controlled directly: source manifests/generators only, generated slab images, or both with reproducible hashes?
- Should core images be content-addressed by manifest/image hash, and how should sessions select an image?
- How should declarative meta-library import images validate ABI/library drift before lazy native binding?
- How should mutable activation frames reference immutable bytecode blobs?
- What is the publication protocol for shared result slabs: epochs, manifests, root ids, tombstones?
- How should Root1 lease slab-id ranges and prevent collisions between tertiary micro-VMs?
- Can low-upward static slab allocation and high-downward dynamic slab allocation work with the current `SlabId` range, or does it require an arena/layer id?
- How should G092 re-entrancy metadata govern which imports can be shared through static core slabs?
- Should Root1 support a forkserver mode as an intermediate before full independent process mounting?
- What lease/epoch/GC protocol keeps published slabs alive while readers have them mapped?

## Implications

- G091 async workers should avoid committing to a JSON-only long-term handoff model; JSON is fine for the first stable boundary, but result-slab publication should remain a target.
- G091 should use a hybrid communication model: JSON/event queues for small control data, published read-only slabs for bulk result data.
- G092 should classify imports not just as safe/unsafe, but also as static-shareable, per-process-rehydratable, thread-safe, process-safe, pure/side-effecting, credential-bearing, worker-allowed, or VM-private.
- G096 should consider layered arena mounts as part of authoritative mmap resume, not only flat root restoration.
- A future Root0 image builder should run at build/static-image generation time, not at every Edict session launch; Root1 and tertiary micro-VMs should mount those slabs instead of repeating bootstrap/import work.
- Static library slabs should be treated as declarative meta-library images: shared metadata/wrappers are mmap'ed read-only, while native handles are bound lazily and locally per process.
- The first read-only slab model should probably treat static core slabs as immortal for their manifest/lease lifetime; mutable refcounts and pinning can be sidecar work if needed later.
- G093 reference-scoped sharing may become less urgent if whole-subtree readonly core/published layers cover the common cases.

## Navigation Guide for Agents

- Load this concept when discussing G091 intern scaling, async worker jobs, mmap-backed VM process isolation, static import sharing, or zero-copy worker communication.
- Pair with 🔗[K032 — Edict Intern Worker Surface](../../Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md) for the current worker API.
- Pair with 🔗[G092 — Cartographer FFI Re-entrancy Metadata](../../Goals/G092-CartographerFfiReentrancyMetadata/index.md) when discussing which imported capabilities can be shared through static slabs.
- Pair with 🔗[G096 — Authoritative mmap Session Resume](../../Goals/G096-AuthoritativeMmapSessionResume/index.md) when discussing arena-layer persistence and remapping.
- Context window impact: Medium.

## Related Items

- 🔗[Dashboard](../../../Dashboard.md)
- 🔗[G091 — Intern Worker Concurrency MVP](../../Goals/G091-InternWorkerConcurrencyMvp/index.md)
- 🔗[G092 — Cartographer FFI Re-entrancy Metadata](../../Goals/G092-CartographerFfiReentrancyMetadata/index.md)
- 🔗[G093 — Reference-Scoped ReadOnly Sharing](../../Goals/G093-ReferenceScopedReadOnlySharing/index.md)
- 🔗[G096 — Authoritative mmap Session Resume](../../Goals/G096-AuthoritativeMmapSessionResume/index.md)
- 🔗[K030 — Arena Persistence Boundary](../../Facts/J3_AgentC/K030_J3_Arena_Persistence.md)
- 🔗[K032 — Edict Intern Worker Surface](../../Facts/J3_AgentC/K032_Edict_Intern_Worker_Surface.md)
- 🔗[WP — G074 Real-time FFI Token Streaming](../../WorkProducts/WP-G074-RealtimeFFITokenStreaming-2026-05-11/index.md)

## Metadata

**Promotion Candidate**: false
**Last Updated**: 2026-05-14
