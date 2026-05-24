# Goal: G103 — Build-Time Static Core Declaration Image MVP

**Status**: ACTIVE / FIRST DECLARATION IMAGE SLICE
**Created**: 2026-05-14  
**Parent**: 🔗[G096 — Authoritative mmap Session Resume](../G096-AuthoritativeMmapSessionResume/index.md)  
**Related Concept**: 🔗[Layered mmap Micro-VM Architecture](../../Concepts/LayeredMmapMicroVmArchitecture/index.md)

## Objective
Prove the first build-time static core image slice: generate a small, versioned, read-only slab/Listree image that contains declarative module/import metadata but no process-local native handles, then mount/read it at runtime.

## Rationale
The full-send slab architecture should not start with a full allocator rewrite. The safer first substrate proof is a **static declaration image**: a build artifact that declares library/module contents as Listree data, validates its manifest/hash/version, and can be mmap-installed by a Root1-style runtime without repeating dynamic import construction.

## Scope
In scope:
- one tiny static declaration image for one module or import namespace;
- a manifest with format/version/hash/root-id fields;
- declarative metadata only: namespace shape, signatures, safety metadata, expected library identity/symbols;
- runtime mount/read/inspect path;
- test coverage proving the image can be generated and consumed without live native handles.

Out of scope:
- full static core image for all builtins/modules;
- process-isolated micro-VM launch;
- published result slabs;
- general layered slab allocator semantics;
- storing raw `dlopen`/`dlsym` handles in static slabs.

## Implementation Plan
- [x] Define the minimal static declaration image manifest schema for the first slice: `format`, `format_version`, `image_kind`, `module`, `root_id`, `hash_algorithm`, `payload_hash`, `contains_native_handles`, `native_binding_policy`, and `forbidden_payloads`.
- [x] Pick one small import/module surface as the first image payload: the `worker.edict` primitive declaration surface, represented as metadata-only symbol declarations.
- [x] Add a deterministic test generator/readback path that emits a Listree/JSON declaration image plus manifest (`edict/static_declaration_image.{h,cpp}`). This is a pre-mmap first slice, not the final slab-file generator.
- [x] Ensure the image contains no process-local handles or mutable VM activation state; symbols declare lazy process-local binding policy and `stores_native_handle: "false"`.
- [x] Add first runtime read-only inspection/mount support sufficient to read the static declaration namespace: `mountDeclarationImageReadOnly(...)` validates the image, recursively freezes it, and marks declaration `ListreeValue` slots static-immortal through the G105 ownership seam. True OS mmap read-only mounting remains a later slice.
- [x] Wire declaration-image mounting to the G105 `ListreeStaticMountRegistry` seam: registry-backed `mountDeclarationImageReadOnly(image, registry)` returns logical mount metadata (`mountId`, `rootId`) while registry lookup/unmount owns the static lease.
- [x] Extend static mount registry metadata beyond process-local ids: registry records now include image id, manifest hash, root descriptor, section descriptor, provenance, Root1-compatible resource descriptor, and first section descriptors/ranges for declaration-image mounts.
- [x] Add true byte-offset/byte-size/hash section descriptors to borrowed `StaticSlotTableView` so read-only slot-table images expose inspectable section provenance without dynamic Listree conversion.
- [x] Wire borrowed static-slot-table section descriptors into G105-compatible mount metadata with image id, payload hash, root descriptor, section descriptors, and Root1-compatible static-slot-table resource descriptor.
- [x] Add stale/incompatible manifest validation for the first schema slice, including payload-hash mismatch rejection.
- [x] Add checked-in tests around deterministic generation, manifest validation, metadata-only policy, and file readback.

## Static Mount Plan — 2026-05-19

The next mount architecture is captured in 🔗[WP — Static Declaration Image Mount Plan](../../WorkProducts/WP-StaticDeclarationImageMountPlan-2026-05-19/index.md). The plan distinguishes four stages:

1. current JSON/Listree bridge: read-only mmap of JSON bytes, parse to dynamic Listree slots, then logical `ReadOnly` + G105 static-immortal slot marking;
2. static binary declaration image container: magic/version/manifest/payload/hash validation while still parsing to dynamic slots;
3. static slot table image: true OS-read-only `ListreeValue`/ref/item/list/tree slot tables with allocator-mounted static slab ranges or a borrowed static-view API;
4. Root1 mount registry: image id, manifest, slab/layer ranges, epochs/leases, and process-local native sidecar identity.

Stage 2's test-only binary container is now implemented. It preserves the metadata-only/no-native-handle invariant while giving G103 a durable byte-format boundary before raw allocator-backed static slot mounting.

The true static slot-table boundary is specified in 🔗[WP — Static Slot Table Image Boundary](../../WorkProducts/WP-StaticSlotTableImageBoundary-2026-05-19/index.md). The first borrowed static slot-table inspector is now implemented before allocator-mounted static slabs: it uses image-local string/declaration/value/list/item/tree ids, validates section lengths and payload hashes, rejects corrupt images, and inspects declarations plus generic object/list/string/tree records directly from read-only mmapped bytes without ordinary `CPtr`/Listree handles. It now also builds a borrowed sorted declaration-word index for dictionary-style lookup and exposes byte-offset/byte-size/hash section descriptors for slot-table provenance without dynamic Listree conversion.

## First Declaration Image Schema — 2026-05-19

The first image is deliberately **declarative metadata only**. It is encoded as a Listree/JSON root with:

```json
{
  "manifest": {
    "format": "agentc.static_declaration_image",
    "format_version": "1",
    "image_kind": "declarative_import_module",
    "module": "worker.edict",
    "root_id": "worker.edict/declarations",
    "hash_algorithm": "fnv1a64",
    "payload_hash": "...",
    "contains_native_handles": "false",
    "native_binding_policy": "lazy_process_local_sidecar",
    "forbidden_payloads": [
      "dlopen_handle",
      "dlsym_pointer",
      "function_pointer",
      "eventfd",
      "epoll_fd",
      "pidfd",
      "edict_vm_pointer",
      "activation_frame",
      "credential",
      "provider_handle"
    ]
  },
  "declarations": []
}
```

Each declaration records the public Edict word, expected native symbol name, stack signature, category, lazy binding policy, worker allowance, and the explicit `stores_native_handle: "false"` assertion. The chosen first payload is the small `worker.edict` primitive surface because it is immediately relevant to G091/G099, has concrete C ABI names, and exercises the important distinction between static declarative namespace metadata and process-local Cartographer/FFI binding sidecars.

This slice intentionally does **not** yet generate mmap slab files or mount OS-read-only mappings. It establishes deterministic image content, manifest/hash validation, handle-exclusion policy, and readback tests so the later mmap/static-slab slice has a stable schema target.

## Implementation Progress — 2026-05-19

- Added `edict/static_declaration_image.h` and `edict/static_declaration_image.cpp`.
- Added `agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage()` for deterministic metadata-only `worker.edict` declaration-image generation.
- Added `writeDeclarationImage(...)`, `readDeclarationImage(...)`, `readDeclarationImageMmapReadOnly(...)`, `validateDeclarationImage(...)`, and deterministic `declarationPayloadHash(...)` helpers.
- Added `mountDeclarationImageReadOnly(...)` as the first runtime inspection/mount seam: it validates the declaration image, recursively applies logical `ReadOnly`, collects declaration `ListreeValue` slots, and marks those exact slots static-immortal via G105 so retain/release and pin/unpin do not mutate static metadata.
- Added registry-backed `mountDeclarationImageReadOnly(image, registry)` as the first G103/G105 image/root metadata bridge: successful mounts expose a process-local logical `mountId`, stable `rootId`, image id, manifest hash, root descriptor, section descriptor, provenance, Root1-compatible resource descriptor, section descriptors (`manifest`, `declarations`) with ranges/counts, registry root lookup after ordinary handles drop, and registry unmount behavior.
- Added `StaticSlotTableSectionDescriptor` plus `StaticSlotTableView::sectionCount()`, `section(...)`, `sectionById(...)`, and `payloadHash()`, covering `string_records`, `declaration_records`, `value_records`, `tree_records`, `item_records`, `list_entries`, and `string_bytes` with file byte offsets, byte sizes, and FNV hashes.
- Added `staticSlotTableMountMetadata(...)`, translating borrowed slot-table section provenance into `ListreeStaticMountMetadata` (`imageId`, `manifestHash`, `rootDescriptor`, `sectionDescriptor`, `provenance`, `root1ResourceDescriptor`, and section offset/size records).
- Added read-only mmap byte import for declaration-image files. This maps the JSON artifact with `PROT_READ` and parses it into a mounted Listree declaration image; true Listree nodes are still heap/private slots marked static-immortal after validation.
- Added a durable static mount design work product that defines the staged path from JSON-byte mmap import to binary containers, static slot tables, and eventual Root1 image mount registry integration.
- Added a test-only static binary declaration-image container with magic/version/manifest-length/payload-length/manifest-json/payload-json bytes. The container is read through `PROT_READ` mmap, validates magic/version/lengths plus manifest payload hash, then mounts through `mountDeclarationImageReadOnly(...)`.
- Added a static slot-table boundary plan that defines image-local ids, section layout, borrowed static view versus allocator-mounted static slab tradeoffs, validation checklist, and the next concrete `StaticSlotTableImage` prototype step.
- Added `edict/static_slot_table_image.{h,cpp}` with `writeStaticSlotTableImage(...)` and `readStaticSlotTableImageMmapReadOnly(...)`. The first borrowed view stores compact string records, declaration records, generic value records, tree records, object item records, and list entry records, exposing module/declaration/object-field inspection without constructing ordinary Listree values from the mapped bytes.
- Added tree/dictionary records for object values. Object fields are stored in sorted item ranges and `objectStringField(...)` uses borrowed binary-search lookup over the read-only mapped table.
- Added `StaticSlotTableView::findDeclarationByWord(...)`, backed by a borrowed sorted declaration-word index built during validation. Duplicate declaration words now fail closed with `duplicate_declaration_word`.
- Added corrupt-image validation for bad magic and payload hash mismatch.
- Added `StaticDeclarationImageTest.WorkerPrimitiveImageIsMetadataOnlyAndValidates`, `StaticDeclarationImageTest.WorkerPrimitiveImageRoundTripsThroughFile`, `StaticDeclarationImageTest.BinaryContainerMmapValidatesAndMountsStaticImmortal`, `StaticDeclarationImageTest.BinaryContainerRejectsInvalidMagic`, `StaticDeclarationImageTest.MmapReadOnlyImageCanBeMountedStaticImmortal`, `StaticDeclarationImageTest.RegistryBackedMountExposesLogicalMountIdAndRootMetadata`, `StaticDeclarationImageTest.ReadOnlyMountMarksDeclarationValueSlotsStaticImmortal`, and `StaticDeclarationImageTest.ValidationRejectsPayloadHashMismatch`.

## Validation — 2026-05-19

- `cmake --build build --target edict_tests -j2` — passed.
- `./build/edict/edict_tests --gtest_filter='StaticSlotTableImageTest.*' --gtest_brief=1` — passed 4/4 after corrupt value/tree/list reference coverage; earlier borrowed static slot-table inspector passed 3/3.
- `./build/edict/edict_tests --gtest_filter='StaticDeclarationImageTest.*' --gtest_brief=1` — passed 8/8 after richer registry-backed declaration-image mount metadata; earlier static binary container work passed 7/7, read-only mmap file import passed 5/5, and static mount slice passed 4/4.
- `./build/edict/edict_tests --gtest_filter='StaticDeclarationImageTest.*:InternWorkerTest.*:Root1AwaitSchedulerTest.*:Root1PrimitiveModuleTest.*:EdictVM.YieldedExecutionCanResumeCurrentCodeFrame' --gtest_brief=1` — passed 25/25 before read-only mmap file import; rerun this slice before final commit.

## Acceptance Criteria
- [x] A static declaration image can be generated deterministically from source metadata for the first `worker.edict` payload.
- [x] Runtime code can inspect/mount the image as logically read-only static-immortal Listree data through the G105 static slot seam; true OS mmap read-only slab data remains a later implementation slice.
- [x] The image manifest records enough version/hash/root-id information to detect stale or incompatible artifacts for the first schema slice.
- [x] No raw native handles are stored as authoritative shared state.
- [x] Documentation explains the distinction between declarative import image and lazy process-local native binding.

## Notes
This goal is intentionally narrower than full Root0/Root1. It creates the first durable artifact that later Root1 and micro-VM workers can mount.
