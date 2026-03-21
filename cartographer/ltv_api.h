// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// AgentC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with AgentC. If not, see <https://www.gnu.org/licenses/>.

#pragma once

// ltv_api.h — C-callable API for ListreeValue (LTV) operations.
//
// This header is intentionally C-compatible so it can be included from
// pure-C source files.
//
// Memory model
// ------------
//   LTV handles are opaque 32-bit slab identifiers.  Lifetime is governed by
//   the slab allocator's reference-counting scheme.
//
//   "Owned reference": the holder is responsible for one unit of refcount.
//     - ltv_create_*()       returns an owned reference (refcount 1).
//     - ltv_unpack_scalar()  returns an owned reference.
//     - ltv_unref()          releases an owned reference.
//
//   "Borrowed reference": valid only while the parent LTV (or another owned
//     reference) is alive.  Do NOT call ltv_unref() on it.
//     - ltv_get_named()      returns a borrowed reference.
//     - The 'child' argument passed to foreach callbacks is borrowed.
//
//   ltv_set_named() does NOT consume the caller's reference; the tree takes
//   its own reference internally.  Caller must ltv_unref() if done with it.

#include <stddef.h>
#include <stdint.h>

/* In C++, SlabId is defined in core/alloc.h (a class wrapping two uint16_t).
 * In C, we define it as uint32_t with the same 4-byte footprint. */
#ifdef __cplusplus
#include "../core/alloc.h"
#else
typedef uint32_t SlabId;
#endif

/* C-domain fundamental fancy-pointer — opaque 32-bit slab handle.
 * Encoded as ((index << 16) | offset).  C code must never inspect the bits. */
typedef SlabId LTV;

/* Null / invalid LTV sentinel (slab index 0, offset 0). */
#define LTV_NULL ((LTV)0)

/* LTV data flags (mirror LtvFlags enum values from listree/listree.h). */
#define LTV_FLAG_BINARY     0x00000008u   /* opaque bytes; use ltv_data/ltv_length */
#define LTV_FLAG_NULL       0x00000200u   /* no payload; empty dict */
#define LTV_FLAG_IMMEDIATE  0x00000400u   /* SSO: data stored inline */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

/** Create a new null (empty dict) LTV. Returns owned reference (refcount 1). */
LTV  ltv_create_null(void);

/** Create a string LTV from len bytes of str (no NUL terminator required).
 *  Returns owned reference. */
LTV  ltv_create_string(const char* str, size_t len);

/** Create a binary LTV, copying len bytes from data.
 *  Returns owned reference. */
LTV  ltv_create_binary(const void* data, size_t len);

/** Increment the refcount of v. No-op if v is NULL. */
void ltv_ref(LTV v);

/** Decrement the refcount of v.  Frees the value if refcount reaches zero.
 *  No-op if v is NULL. */
void ltv_unref(LTV v);

/* ---------------------------------------------------------------------------
 * Data access
 * ------------------------------------------------------------------------- */

/** Return the flags bitmask.  See LTV_FLAG_* constants. */
uint32_t ltv_flags(LTV v);

/** Return a pointer to the raw data bytes, or NULL for null/empty values. */
const void* ltv_data(LTV v);

/** Return the byte length of the data payload. */
size_t ltv_length(LTV v);

/* ---------------------------------------------------------------------------
 * Named children (AA-tree dict operations)
 * ------------------------------------------------------------------------- */

/** Look up the named child of parent.
 *  Returns a BORROWED reference — valid only while parent is alive.
 *  Do NOT call ltv_unref() on the result.
 *  Returns NULL if the child is not found. */
LTV ltv_get_named(LTV parent, const char* name);

/** Insert or replace the named child of parent.
 *  The tree takes its own reference; the caller retains their reference and
 *  must call ltv_unref() if they no longer need it. */
void ltv_set_named(LTV parent, const char* name, LTV child);

/** Iterate all named children of parent in lexicographic order.
 *  For each child, cb(name, child, ud) is called.
 *  The 'child' passed to the callback is a BORROWED reference — do NOT call
 *  ltv_unref() on it inside the callback. */
void ltv_foreach_named(LTV parent,
                       void (*cb)(const char* name, LTV child, void* ud),
                       void* ud);

/* ---------------------------------------------------------------------------
 * Scalar pack / unpack  (bridges to Boxing::packScalar / unpackScalar)
 * ------------------------------------------------------------------------- */

/** Pack a scalar field from an LTV value into raw memory at dest.
 *  ctype  — null-terminated C type name (e.g. "int", "unsigned long").
 *  val    — BORROWED reference to source LTV (may be NULL → zero-fill).
 *  dest   — raw memory destination (must be large enough for ctype).
 *  Returns 0 on success, non-zero if ctype is unrecognised (dest unchanged). */
int ltv_pack_scalar(const char* ctype, LTV val, void* dest);

/** Unpack a scalar from raw memory src into a new string LTV.
 *  ctype  — null-terminated C type name.
 *  src    — raw memory source.
 *  Returns an OWNED reference (refcount 1).
 *  Returns a null LTV for unrecognised types. */
LTV ltv_unpack_scalar(const char* ctype, const void* src);

#ifdef __cplusplus
} /* extern "C" */

/* ---------------------------------------------------------------------------
 * C++ boundary utilities (not callable from C)
 * These are used by ffi.cpp and edict_vm.cpp to translate between
 * CPtr<ListreeValue> (which lives on the Edict data stack) and LTV handles
 * (passed to/from C-ABI functions).
 * ------------------------------------------------------------------------- */

#include "../listree/listree.h"

namespace agentc {

/** CPtr<ListreeValue> → LTV: O(1), no refcount change.
 *  The returned LTV is a borrowed handle — keep the CPtr alive. */
inline LTV cptr_to_ltv(const CPtr<ListreeValue>& p) {
    return p.getSlabId();
}

/** LTV → CPtr (BORROW): addref on construction, decref on destruction.
 *  Use for arguments that are valid for the duration of the CPtr's lifetime. */
inline CPtr<ListreeValue> ltv_borrow(LTV v) {
    return CPtr<ListreeValue>(v);
}

/** LTV → CPtr (OWN): no addref — caller transfers ownership.
 *  Use when a C function returns an owned reference (e.g. ltv_create_*). */
inline CPtr<ListreeValue> ltv_adopt(LTV v) {
    return CPtr<ListreeValue>::adoptRaw(v);
}

} // namespace agentc

#endif /* __cplusplus */
