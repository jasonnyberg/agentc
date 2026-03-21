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

// boxing_ffi.c — Pure C implementation of agentc_box / agentc_unbox /
// agentc_box_free built on top of the ltv_api.h C API.
//
// This file has NO C++ dependencies and can be compiled with a plain C
// compiler.  It mirrors the logic in cartographer/boxing.cpp but expressed
// entirely through ltv_api.h primitives.

#include "boxing_ffi.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Read a 4-byte little-endian int from a binary LTV.
// Returns -1 on error (wrong type, too short, NULL).
static int32_t read_binary_int32(LTV v) {
    if (!v) return -1;
    if (!(ltv_flags(v) & LTV_FLAG_BINARY)) return -1;
    if (ltv_length(v) < 4) return -1;
    int32_t val = 0;
    memcpy(&val, ltv_data(v), 4);
    return val;
}

// Read a NUL-terminated C string from a string LTV into buf[bufsz].
// Returns the number of bytes written (not counting the NUL), or 0 on error.
static size_t read_string(LTV v, char* buf, size_t bufsz) {
    if (!v || !buf || bufsz == 0) return 0;
    if (ltv_flags(v) & LTV_FLAG_BINARY) return 0;
    size_t len = ltv_length(v);
    if (len == 0) { buf[0] = '\0'; return 0; }
    if (len >= bufsz) len = bufsz - 1;
    memcpy(buf, ltv_data(v), len);
    buf[len] = '\0';
    return len;
}

// ---------------------------------------------------------------------------
// Callbacks for ltv_foreach_named used during box and unbox
// ---------------------------------------------------------------------------

// Context for box_field_cb: used to pack one field into the heap buffer.
typedef struct {
    LTV source;         // source field-keyed LTV (borrowed)
    uint8_t* base;      // base of heap buffer
} BoxContext;

// For each field-def child of typeDef's "children" node:
//   read kind/type/offset, look up field in source, pack into base.
static void box_field_cb(const char* fieldName, LTV fieldDef, void* ud) {
    BoxContext* ctx = (BoxContext*)ud;

    // kind must be "Field"
    LTV kindVal = ltv_get_named(fieldDef, "kind");
    char kindBuf[16];
    if (read_string(kindVal, kindBuf, sizeof(kindBuf)) == 0) return;
    if (strcmp(kindBuf, "Field") != 0) return;

    // Get field type name
    LTV typeVal = ltv_get_named(fieldDef, "type");
    char typeBuf[128];
    if (read_string(typeVal, typeBuf, sizeof(typeBuf)) == 0) return;

    // Get byte offset
    LTV offsetVal = ltv_get_named(fieldDef, "offset");
    int32_t byteOffset = read_binary_int32(offsetVal);
    if (byteOffset < 0) return;

    // Look up field value in source
    LTV srcVal = ctx->source ? ltv_get_named(ctx->source, fieldName) : NULL;

    // Pack scalar field
    ltv_pack_scalar(typeBuf, srcVal, ctx->base + byteOffset);
}

// Trampoline: foreach on typeDef's "children" sub-tree.
static void box_children_cb(const char* name, LTV child, void* ud) {
    (void)name;
    // "children" is the one child we care about — iterate its fields.
    ltv_foreach_named(child, box_field_cb, ud);
}

// Context for unbox_field_cb: used to unpack one field from heap buffer.
typedef struct {
    const uint8_t* base; // base of heap buffer (read-only)
    LTV result;          // target LTV (owned, we add named items to it)
} UnboxContext;

static void unbox_field_cb(const char* fieldName, LTV fieldDef, void* ud) {
    UnboxContext* ctx = (UnboxContext*)ud;

    LTV kindVal = ltv_get_named(fieldDef, "kind");
    char kindBuf[16];
    if (read_string(kindVal, kindBuf, sizeof(kindBuf)) == 0) return;
    if (strcmp(kindBuf, "Field") != 0) return;

    LTV typeVal = ltv_get_named(fieldDef, "type");
    char typeBuf[128];
    if (read_string(typeVal, typeBuf, sizeof(typeBuf)) == 0) return;

    LTV offsetVal = ltv_get_named(fieldDef, "offset");
    int32_t byteOffset = read_binary_int32(offsetVal);
    if (byteOffset < 0) return;

    // Unpack scalar — returns owned reference
    LTV fieldVal = ltv_unpack_scalar(typeBuf, ctx->base + byteOffset);
    if (fieldVal) {
        ltv_set_named(ctx->result, fieldName, fieldVal);
        ltv_unref(fieldVal);   // tree took its own ref
    }
}

static void unbox_children_cb(const char* name, LTV child, void* ud) {
    (void)name;
    ltv_foreach_named(child, unbox_field_cb, ud);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

LTV agentc_box(LTV source, LTV typeDef) {
    if (!typeDef) return NULL;

    // Read struct size from typeDef's "size" field (4-byte binary int).
    LTV sizeVal = ltv_get_named(typeDef, "size");
    int32_t structSize = read_binary_int32(sizeVal);
    if (structSize <= 0) return NULL;

    // Allocate zeroed heap buffer.
    uint8_t* buf = (uint8_t*)calloc(1, (size_t)structSize);
    if (!buf) return NULL;

    // Pack fields: iterate typeDef's "children" child.
    BoxContext ctx;
    ctx.source = source;
    ctx.base = buf;

    // typeDef has a named child "children" whose children are field defs.
    ltv_foreach_named(typeDef, box_children_cb, &ctx);

    // Build boxed LTV: { __ptr: binary[8], __type: typeDef }
    LTV boxed = ltv_create_null();
    if (!boxed) { free(buf); return NULL; }

    // Store pointer-to-pointer in __ptr: boxing.cpp stores &buf (address of
    // local), so the binary blob holds the heap address of the struct.
    LTV ptrVal = ltv_create_binary(&buf, sizeof(void*));
    if (!ptrVal) { free(buf); ltv_unref(boxed); return NULL; }
    ltv_set_named(boxed, "__ptr", ptrVal);
    ltv_unref(ptrVal);

    // Attach __type — tree takes its own ref; caller retains their ref to typeDef.
    ltv_ref(typeDef);
    ltv_set_named(boxed, "__type", typeDef);
    ltv_unref(typeDef);

    return boxed; // owned, refcount 1
}

LTV agentc_unbox(LTV boxed) {
    if (!boxed) return NULL;

    // Extract __ptr
    LTV ptrVal = ltv_get_named(boxed, "__ptr");
    if (!ptrVal) return NULL;
    if (!(ltv_flags(ptrVal) & LTV_FLAG_BINARY)) return NULL;
    if (ltv_length(ptrVal) != sizeof(void*)) return NULL;

    void* rawPtr = NULL;
    memcpy(&rawPtr, ltv_data(ptrVal), sizeof(void*));
    if (!rawPtr) return NULL;

    // Extract __type
    LTV typeDef = ltv_get_named(boxed, "__type");
    if (!typeDef) return NULL;

    // Unpack struct
    LTV result = ltv_create_null();
    if (!result) return NULL;

    UnboxContext ctx;
    ctx.base = (const uint8_t*)rawPtr;
    ctx.result = result;

    ltv_foreach_named(typeDef, unbox_children_cb, &ctx);

    // Attach __type annotation
    ltv_ref(typeDef);
    ltv_set_named(result, "__type", typeDef);
    ltv_unref(typeDef);

    return result; // owned, refcount 1
}

void agentc_box_free(LTV boxed) {
    if (!boxed) return;

    LTV ptrVal = ltv_get_named(boxed, "__ptr");
    if (!ptrVal) return;
    if (!(ltv_flags(ptrVal) & LTV_FLAG_BINARY)) return;
    if (ltv_length(ptrVal) != sizeof(void*)) return;

    void* rawPtr = NULL;
    memcpy(&rawPtr, ltv_data(ptrVal), sizeof(void*));
    if (rawPtr) free(rawPtr);
}
