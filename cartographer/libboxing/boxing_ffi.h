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

// boxing_ffi.h — C-callable boxing/unboxing API built on top of ltv_api.h.
//
// This header is intentionally pure C so it can be included from C or C++
// files.  It mirrors the semantics of the Edict VMOP_BOX / VMOP_UNBOX /
// VMOP_BOX_FREE opcodes but as plain C functions callable over an FFI boundary.
//
// Ownership contract (follows ltv_api.h conventions)
// ---------------------------------------------------
//   agentc_box()      — returns an OWNED LTV reference (refcount 1).
//                       The returned boxed LTV embeds a __ptr (heap allocation)
//                       that must eventually be freed with agentc_box_free().
//
//   agentc_unbox()    — returns an OWNED LTV reference (refcount 1).
//                       The heap allocation inside boxed is NOT freed; caller
//                       must call agentc_box_free() separately when done.
//
//   agentc_box_free() — frees the heap allocation inside a boxed LTV.
//                       Does NOT call ltv_unref() on the LTV itself.
//
// Both source/typeDef arguments to agentc_box() are BORROWED (not consumed).
// The boxed argument to agentc_unbox() / agentc_box_free() is BORROWED.

#include "../ltv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Allocate a C struct on the heap, pack fields from source into it, and
 *  return a boxed LTV { __ptr: binary[8], __type: typeDef }.
 *
 *  source  — field-keyed LTV whose fields match typeDef's children (BORROWED).
 *  typeDef — the struct's type-definition node from the parser namespace (BORROWED).
 *
 *  Returns an owned LTV on success, or NULL on error (unknown size, alloc
 *  failure, etc.).  Call ltv_unref() and agentc_box_free() when done. */
LTV agentc_box(LTV source, LTV typeDef);

/** Read a C struct from a boxed LTV's __ptr and produce a field-keyed LTV.
 *
 *  boxed — { __ptr: binary[8], __type: type-def-LTV } (BORROWED).
 *
 *  Returns an owned LTV on success, or NULL on error.
 *  The heap allocation inside boxed is NOT freed by this call. */
LTV agentc_unbox(LTV boxed);

/** Free the heap allocation stored in a boxed LTV's __ptr field.
 *
 *  boxed — { __ptr: binary[8], ... } (BORROWED).
 *  Safe to call with NULL. */
void agentc_box_free(LTV boxed);

#ifdef __cplusplus
} /* extern "C" */
#endif
