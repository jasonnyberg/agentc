# Work Product: Static Slot Table Image Boundary

**Date**: 2026-05-19  
**Goal**: 🔗[G103 — Build-Time Static Core Declaration Image MVP](../../Goals/G103-BuildTimeStaticCoreDeclarationImageMvp/index.md)  
**Related**: 🔗[G105 — ReadOnly Static Slab Ownership Model](../../Goals/G105-ReadOnlyStaticSlabOwnershipModel/index.md), 🔗[G104 — Immutable Code Object / Activation Frame Split](../../Goals/G104-ImmutableCodeObjectActivationFrameSplit/index.md), 🔗[G096 — Authoritative mmap Session Resume](../../Goals/G096-AuthoritativeMmapSessionResume/index.md), 🔗[G106 — Root1 Slab Advertisement Registry](../../Goals/G106-Root1SlabAdvertisementRegistry/index.md), 🔗[G107 — Process-Isolated Micro-VM Interns](../../Goals/G107-ProcessIsolatedMicroVmInterns/index.md), 🔗[WP — Static Declaration Image Mount Plan](../WP-StaticDeclarationImageMountPlan-2026-05-19/index.md)

## Purpose

Define the true static slot-table boundary that follows the G103 binary declaration container. This boundary explains how `ListreeValue`, `ListreeValueRef`, `ListreeItem`, list/tree nodes, and string/blob payloads should be represented in a metadata-only image without native authority handles, and how those image-local records later become readable through either borrowed static views or allocator-mounted static slabs.

## Current constraint

The current G103 container is durable bytes, but not a static slot table:

```text
container bytes --PROT_READ--> JSON strings --parse--> dynamic Listree slots --mark static-immortal
```

The next true-static step must move toward:

```text
container bytes --PROT_READ--> typed static slot records --mount table--> read-only Listree namespace
```

without writing allocator `inUse`, node-local `pinnedCount`, activation frames, native handles, or capability sidecars into the mapped image.

## Core design rule

Static declaration images are **data and schema**, not authority.

They may contain:

- module/namespace names;
- Edict word names;
- native symbol names as text;
- stack/signature metadata;
- safety/re-entrancy metadata;
- expected library identity/hash/version metadata;
- static string/blob payloads;
- static Listree structure records.

They must not contain:

- raw `dlopen`/`dlsym` handles;
- function pointers;
- provider/runtime/session handles;
- eventfd/epoll/pidfd/raw fd values;
- live `EdictVM*` pointers;
- mutable activation frames or instruction pointers;
- cancellation tokens, job records, locks, or resource owner words.

## Static image-local ids

A static slot table should not initially assume image-local ids are ordinary dynamic `SlabId`s. Use explicit image-local ids:

```text
StaticValueId  = uint32
StaticRefId    = uint32
StaticItemId   = uint32
StaticListId   = uint32
StaticTreeId   = uint32
StaticBlobId   = uint32
```

Rationale:

- avoids widening or overloading current `SlabId` before G096/G104 layered-id decisions;
- permits validation before exposing any public handle;
- keeps the image portable across processes even when allocator slab ranges differ;
- lets G106/Root1 later assign mount-local slab/layer ranges.

## Proposed table sections

A declaration image static slot table should contain:

```text
[Header]
[Manifest]
[String/Blob table]
[ListreeValue table]
[ListreeValueRef table]
[ListreeItem table]
[List node table]
[Tree node table]
[Root index]
[Payload hash / section hashes]
```

### Header

Fields:

- magic, e.g. `ACSTBL01`;
- format version;
- byte order marker;
- section count;
- offsets/lengths;
- manifest hash;
- payload hash.

### Manifest

Extends current G103 manifest with:

- `format: agentc.static_slot_table_image`;
- `format_version`;
- `source_image_kind: declarative_import_module`;
- `module`;
- `root_static_value_id`;
- `contains_native_handles: false`;
- `native_binding_policy: lazy_process_local_sidecar`;
- `section_hashes`;
- optional `required_mount_capabilities`.

### String/blob table

Stores immutable bytes for:

- string values;
- field names;
- symbol names;
- manifest strings;
- notes/documentation.

Records:

```text
blob_id, offset, length, flags(binary/string)
```

The blob table is read-only. `ListreeValue::getData()` for static string values should ultimately point to read-only blob bytes or a borrowed view, not duplicated mutable heap data.

### ListreeValue table

Records enough to reconstruct or view `ListreeValue` without mutable operational fields:

```text
value_id
mode/null/list/tree/string/blob
flags_for_semantics
payload_blob_id or null
list_id or null
tree_id or null
```

Excluded from static value records:

- live allocator refcount;
- `pinnedCount`;
- process-local data pointers;
- mutable traversal state;
- activation-frame state.

### ListreeValueRef table

Records:

```text
ref_id
value_id
```

No mutable ref ownership metadata.

### List node table

The current dynamic representation uses `CLL<ListreeValueRef>`. Static table can represent lists as arrays first:

```text
list_id
ordered_ref_ids[]
```

A later allocator-mounted static slab can materialize CLL-compatible nodes or expose a static list iterator over array records.

### ListreeItem table

Records:

```text
item_id
name_blob_id
history_ref_ids[]   // newest/oldest order must be explicit
```

This preserves Edict/Listree history semantics without permitting mutation/pop of static histories.

### Tree node table

The current dynamic representation uses `AATree<ListreeItem>`. Static table should not need to persist AA-tree balancing internals at first. Prefer canonical sorted item arrays:

```text
tree_id
item_ids_sorted_by_name[]
```

A borrowed static view can binary-search this array. An allocator-mounted static slab implementation can later materialize an AA-tree-compatible layout if required for direct reuse.

## Mount models

### Model A — borrowed static view first

Expose a read-only static image view without converting image-local ids to ordinary `CPtr`:

```text
StaticImageView::root()
StaticValueView::find(name)
StaticValueView::forEachList(...)
StaticValueView::toJson()
```

Pros:

- avoids immediate allocator surgery;
- safe over true `PROT_READ` mapping;
- image-local ids stay explicit;
- good for manifest inspection and G103 tests.

Cons:

- Edict/VM lookup cannot transparently use static dictionaries yet;
- requires adapter code or copy/promotion when ordinary `CPtr` is needed.

### Model B — allocator-mounted static slabs

Mount static slot tables into allocator-visible static slab ranges:

```text
Allocator<T>::mountStaticSlabRange(range, mapped_items, static_metadata)
```

Pros:

- existing Edict/Listree APIs can traverse mounted roots naturally;
- aligns with future static core dictionaries.

Cons:

- requires static-aware `getPtr`, `valid`, `refs`, `tryRetain`, `deallocate`, rollback, and allocator reset behavior;
- requires static representations compatible with current C++ object layout or a safe static wrapper layer;
- more dangerous before G104/G096 settle layered ids and activation frame separation.

## Recommendation

Implement Model A first as a **borrowed static slot table inspector**.

Acceptance for the next code slice:

- build a static slot-table image from the current `worker.edict` declaration image;
- write it as a binary container with section offsets/hashes;
- mmap it `PROT_READ`;
- validate header/manifest/section hashes;
- inspect root/module/declarations through static image-local ids without constructing ordinary mutable Listree nodes;
- prove no native authority handles are stored;
- optionally convert/copy into dynamic Listree only after validation for compatibility with existing `mountDeclarationImageReadOnly(...)`.

After that, Model B can be attempted with a tiny test-only allocator static slab range.

## Mount validation checklist

A static slot-table mount must validate:

- magic/version/endianness;
- section offsets and lengths are in bounds and non-overlapping;
- root id exists;
- every referenced value/ref/item/blob id exists;
- tree item names are sorted and unique for first dictionary view;
- list/history order is explicit and stable;
- `contains_native_handles == false`;
- all native capability fields are text metadata only;
- payload/section hashes match;
- unknown required mount capability fails closed.

## Interaction with G105

G105's static-immortal slot seam remains useful for dynamic compatibility after a static image is copied/promoted into ordinary Listree nodes, but true static slot-table inspection should avoid ordinary `CPtr` entirely until the allocator-mounted static slab model exists.

When Model B starts, G105 must extend static ownership to:

- all involved allocator types (`ListreeValue`, `ListreeValueRef`, `ListreeItem`, `CLL`, `AATree`, blobs if applicable);
- no-op or sidecar `pinnedCount` behavior;
- rollback/checkpoint paths;
- allocator reset/unmount semantics;
- tests for actual read-only mapped pages.

## Interaction with G104/G096

Do not store activation frames, instruction pointers, or mutable code execution state in declaration slot tables. G104 owns the split between immutable code objects and mutable activation frames. G096 owns durable session resume and layered mount identity. G103 should stop at static declaration/module metadata until those designs are ready.

## Next concrete code step

Add a `StaticSlotTableImage` prototype for declaration metadata:

1. Define a compact binary table format for strings, values, list arrays, item arrays, and tree arrays.
2. Implement builder from current `worker.edict` declaration image.
3. Implement `StaticSlotTableView` that can inspect module/root/declaration count and read symbol names directly from mmap bytes.
4. Add corrupt-reference/hash tests.
5. Keep conversion to dynamic Listree optional and explicit.
