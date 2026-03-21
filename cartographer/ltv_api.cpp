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

// Compile-time sanity: LTV_FLAG_* constants must match LtvFlags enum values.
static_assert(static_cast<uint32_t>(agentc::LtvFlags::Binary)    == LTV_FLAG_BINARY,    "LTV_FLAG_BINARY mismatch");
static_assert(static_cast<uint32_t>(agentc::LtvFlags::Null)      == LTV_FLAG_NULL,      "LTV_FLAG_NULL mismatch");
static_assert(static_cast<uint32_t>(agentc::LtvFlags::Immediate) == LTV_FLAG_IMMEDIATE, "LTV_FLAG_IMMEDIATE mismatch");

// ---------------------------------------------------------------------------
// Internal helper: extract raw pointer from a CPtr while preserving refcount.
//
// createNullValue() etc. return a CPtr with refcount 1.  If we let the CPtr
// destruct at the end of this function the refcount drops to 0 and the value
// is freed.  We pre-increment the refcount by 1 so that after the CPtr
// destructs the caller is left holding exactly one reference.
// ---------------------------------------------------------------------------
static agentc::ListreeValue* cptr_release(CPtr<agentc::ListreeValue> p) {
    auto* raw = static_cast<agentc::ListreeValue*>(p);
    if (!raw) return nullptr;
    auto& alloc = Allocator<agentc::ListreeValue>::getAllocator();
    auto si = alloc.getSlabId(raw);
    alloc.modrefs(si, +1);   // +1 to compensate for the upcoming ~CPtr(-1)
    return raw;              // p destructs: -1; net = 0; caller owns refcount 1
}

extern "C" {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

LTV ltv_create_null(void) {
    return cptr_release(agentc::createNullValue());
}

LTV ltv_create_string(const char* str, size_t len) {
    if (!str) return cptr_release(agentc::createNullValue());
    return cptr_release(agentc::createStringValue(std::string(str, len)));
}

LTV ltv_create_binary(const void* data, size_t len) {
    if (!data) return cptr_release(agentc::createNullValue());
    return cptr_release(agentc::createBinaryValue(data, len));
}

void ltv_ref(LTV v) {
    if (!v) return;
    auto* lv = static_cast<agentc::ListreeValue*>(v);
    auto& alloc = Allocator<agentc::ListreeValue>::getAllocator();
    auto si = alloc.getSlabId(lv);
    if (alloc.valid(si)) alloc.modrefs(si, +1);
}

void ltv_unref(LTV v) {
    if (!v) return;
    auto* lv = static_cast<agentc::ListreeValue*>(v);
    auto& alloc = Allocator<agentc::ListreeValue>::getAllocator();
    auto si = alloc.getSlabId(lv);
    if (alloc.valid(si)) alloc.deallocate(si);
}

// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

uint32_t ltv_flags(LTV v) {
    if (!v) return LTV_FLAG_NULL;
    auto* lv = static_cast<agentc::ListreeValue*>(v);
    return static_cast<uint32_t>(lv->getFlags());
}

const void* ltv_data(LTV v) {
    if (!v) return nullptr;
    return static_cast<agentc::ListreeValue*>(v)->getData();
}

size_t ltv_length(LTV v) {
    if (!v) return 0;
    return static_cast<agentc::ListreeValue*>(v)->getLength();
}

// ---------------------------------------------------------------------------
// Named children
// ---------------------------------------------------------------------------

LTV ltv_get_named(LTV parent, const char* name) {
    if (!parent || !name) return nullptr;
    auto* lv = static_cast<agentc::ListreeValue*>(parent);
    auto item = lv->find(std::string(name));
    if (!item) return nullptr;
    // getValue() copy-constructs a CPtr (addref).  We extract the raw pointer
    // and let the local CPtr destruct (decref), leaving the tree's reference
    // intact.  The returned pointer is a BORROWED reference.
    CPtr<agentc::ListreeValue> val = item->getValue(false, false);
    return static_cast<LTV>(static_cast<agentc::ListreeValue*>(val));
}

void ltv_set_named(LTV parent, const char* name, LTV child) {
    if (!parent || !name || !child) return;
    auto* parentLv = static_cast<agentc::ListreeValue*>(parent);
    // Build a temporary CPtr for parent so addNamedItem can take its non-const ref.
    CPtr<agentc::ListreeValue> parentCPtr(parentLv);
    CPtr<agentc::ListreeValue> childCPtr(static_cast<agentc::ListreeValue*>(child));
    agentc::addNamedItem(parentCPtr, std::string(name), childCPtr);
    // Both CPtrs destruct, releasing their temporary references.
    // The tree holds its own reference to child now.
}

void ltv_foreach_named(LTV parent,
                       void (*cb)(const char* name, LTV child, void* ud),
                       void* ud) {
    if (!parent || !cb) return;
    auto* lv = static_cast<agentc::ListreeValue*>(parent);
    lv->forEachTree([&](const std::string& name, CPtr<agentc::ListreeItem>& item) {
        if (!item) return;
        // getValue() addref's — the local CPtr keeps child alive during the callback.
        CPtr<agentc::ListreeValue> val = item->getValue(false, false);
        auto* rawVal = static_cast<agentc::ListreeValue*>(val);
        if (rawVal) {
            cb(name.c_str(), static_cast<LTV>(rawVal), ud);
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
    CPtr<agentc::ListreeValue> valCPtr;
    if (val) {
        valCPtr = CPtr<agentc::ListreeValue>(
            static_cast<agentc::ListreeValue*>(val));
    }
    agentc::cartographer::Boxing::packScalar(typeStr, valCPtr, dest);
    return 0;
}

LTV ltv_unpack_scalar(const char* ctype, const void* src) {
    if (!ctype || !src) return cptr_release(agentc::createNullValue());
    return cptr_release(
        agentc::cartographer::Boxing::unpackScalar(std::string(ctype), src));
}

} // extern "C"
