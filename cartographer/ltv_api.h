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
// pure-C source files (e.g. libboxing/boxing_ffi.c).
//
// Memory model
// ------------
//   LTV handles are raw ListreeValue* pointers.  Lifetime is governed by the
//   slab allocator's reference-counting scheme.
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

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle: raw ListreeValue pointer. */
typedef void* LTV;

/* LTV data flags (mirror LtvFlags enum values from listree/listree.h). */
#define LTV_FLAG_BINARY     0x00000008u   /* opaque bytes; use ltv_data/ltv_length */
#define LTV_FLAG_NULL       0x00000200u   /* no payload; empty dict */
#define LTV_FLAG_IMMEDIATE  0x00000400u   /* SSO: data stored inline */

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
 * These are used by ffi.cpp to translate between CPtr<ListreeValue> (which
 * lives on the Edict data stack) and raw LTV handles (passed to C code).
 * ------------------------------------------------------------------------- */

#include "../listree/listree.h"
#include "../core/alloc.h"

namespace agentc {

/** Extract raw ListreeValue* from a CPtr without changing refcount.
 *  The raw pointer is valid only while the CPtr (or another owner) is alive. */
inline LTV cptr_to_raw(const CPtr<ListreeValue>& p) {
    return static_cast<LTV>(static_cast<ListreeValue*>(p));
}

/** Wrap a raw ListreeValue* into a CPtr, incrementing its refcount.
 *  Use for BORROWED references (shared ownership). */
inline CPtr<ListreeValue> raw_to_cptr(LTV v) {
    return CPtr<ListreeValue>(static_cast<ListreeValue*>(v));
}

/** Adopt a raw ListreeValue* into a CPtr WITHOUT incrementing refcount.
 *  Use for OWNED references transferred from C code (e.g. ltv_create_*
 *  return values) — the CPtr takes over the existing refcount 1. */
inline CPtr<ListreeValue> raw_adopt_cptr(LTV v) {
    if (!v) return nullptr;
    auto* lv = static_cast<ListreeValue*>(v);
    auto& alloc = Allocator<ListreeValue>::getAllocator();
    auto si = alloc.getSlabId(lv);
    return CPtr<ListreeValue>::adoptRaw(si);
}

} // namespace agentc

#endif /* __cplusplus */
