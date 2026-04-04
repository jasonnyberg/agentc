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

#include "ltv_api.h"
#include "boxing.h"
#include "../listree/listree.h"
#include "../core/alloc.h"
#include <cassert>
#include <cstring>
#include <string>

using LV = agentc::ListreeValue;

// Compile-time sanity: LTV_FLAG_* constants must match LtvFlags enum values.
static_assert(static_cast<uint32_t>(agentc::LtvFlags::Binary)    == LTV_FLAG_BINARY,    "LTV_FLAG_BINARY mismatch");
static_assert(static_cast<uint32_t>(agentc::LtvFlags::Null)      == LTV_FLAG_NULL,      "LTV_FLAG_NULL mismatch");
static_assert(static_cast<uint32_t>(agentc::LtvFlags::Immediate) == LTV_FLAG_IMMEDIATE, "LTV_FLAG_IMMEDIATE mismatch");

// Compile-time check: LTV (= SlabId) and uint32_t have the same size so that
// the C-domain uint32_t and C++-domain SlabId can be exchanged safely at ABI
// boundaries that go through a 4-byte encoding.
static_assert(sizeof(LTV) == sizeof(uint32_t), "SlabId must be 4 bytes to match C-domain uint32_t");

extern "C" {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LTV ltv_create_null(void) {
    return agentc::createNullValue().release();
}

LTV ltv_create_string(const char* str, size_t len) {
    if (!str) return agentc::createNullValue().release();
    return agentc::createStringValue(std::string(str, len)).release();
}

LTV ltv_create_binary(const void* data, size_t len) {
    if (!data) return agentc::createNullValue().release();
    return agentc::createBinaryValue(data, len).release();
}

void ltv_ref(LTV v) {
    if (v == LTV_NULL) return;
    (void)Allocator<LV>::getAllocator().tryRetain(v);
}

void ltv_unref(LTV v) {
    if (v == LTV_NULL) return;
    auto& alloc = Allocator<LV>::getAllocator();
    if (alloc.valid(v)) alloc.deallocate(v);
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

uint32_t ltv_flags(LTV v) {
    if (v == LTV_NULL) return LTV_FLAG_NULL;
    auto* lv = Allocator<LV>::getAllocator().getPtr(v);
    if (!lv) return LTV_FLAG_NULL;
    return static_cast<uint32_t>(lv->getFlags());
}

const void* ltv_data(LTV v) {
    if (v == LTV_NULL) return nullptr;
    auto* lv = Allocator<LV>::getAllocator().getPtr(v);
    return lv ? lv->getData() : nullptr;
}

size_t ltv_length(LTV v) {
    if (v == LTV_NULL) return 0;
    auto* lv = Allocator<LV>::getAllocator().getPtr(v);
    return lv ? lv->getLength() : 0;
}

// ---------------------------------------------------------------------------
// Named children
// ---------------------------------------------------------------------------

LTV ltv_get_named(LTV parent, const char* name) {
    if (parent == LTV_NULL || !name) return LTV_NULL;
    auto* lv = Allocator<LV>::getAllocator().getPtr(parent);
    if (!lv) return LTV_NULL;
    auto item = lv->find(std::string(name));
    if (!item) return LTV_NULL;
    // getValue() copy-constructs a CPtr (addref). Extract the SlabId via
    // cptr_to_ltv, then let the local CPtr destruct (decref). The returned
    // SlabId is a BORROWED reference — valid only while the tree (parent) is alive.
    CPtr<LV> val = item->getValue(false, false);
    return agentc::cptr_to_ltv(val);
}

void ltv_set_named(LTV parent, const char* name, LTV child) {
    if (parent == LTV_NULL || !name || child == LTV_NULL) return;
    CPtr<LV> parentCPtr = agentc::ltv_borrow(parent);
    CPtr<LV> childCPtr  = agentc::ltv_borrow(child);
    agentc::addNamedItem(parentCPtr, std::string(name), childCPtr);
    // Both CPtrs destruct here, releasing their temporary borrow references.
    // The tree holds its own reference to child.
}

void ltv_foreach_named(LTV parent,
                       void (*cb)(const char* name, LTV child, void* ud),
                       void* ud) {
    if (parent == LTV_NULL || !cb) return;
    auto* lv = Allocator<LV>::getAllocator().getPtr(parent);
    if (!lv) return;
    lv->forEachTree([&](const std::string& name, CPtr<agentc::ListreeItem>& item) {
        if (!item) return;
        // getValue() addref's — the local CPtr keeps child alive during callback.
        CPtr<LV> val = item->getValue(false, false);
        LTV childLtv = agentc::cptr_to_ltv(val);
        if (childLtv != LTV_NULL) {
            cb(name.c_str(), childLtv, ud);
        }
        // val destructs here; child refcount returns to tree's value.
    });
}

// ---------------------------------------------------------------------------
// Scalar pack / unpack
// ---------------------------------------------------------------------------

int ltv_pack_scalar(const char* ctype, LTV val, void* dest) {
    if (!ctype || !dest) return -1;
    std::string typeStr(ctype);
    if (agentc::cartographer::Boxing::scalarSize(typeStr) == 0) return 1; // unrecognised
    CPtr<LV> valCPtr;
    if (val != LTV_NULL) {
        valCPtr = agentc::ltv_borrow(val);
    }
    agentc::cartographer::Boxing::packScalar(typeStr, valCPtr, dest);
    return 0;
}

LTV ltv_unpack_scalar(const char* ctype, const void* src) {
    if (!ctype || !src) return agentc::createNullValue().release();
    return agentc::cartographer::Boxing::unpackScalar(std::string(ctype), src).release();
}

} // extern "C"
