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

#include "boxing.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>

namespace agentc {
namespace cartographer {

namespace {

// Helper: read a string from an LTV value (non-binary string data only).
// Returns empty string if the value is null, binary, or has no data.
static std::string ltvString(CPtr<ListreeValue> v) {
    if (!v || !v->getData() || v->getLength() == 0) return {};
    if ((v->getFlags() & LtvFlags::Binary) != LtvFlags::None) return {};
    return std::string(static_cast<const char*>(v->getData()), v->getLength());
}

// Helper: read a 4-byte little-endian int from binary LTV (size/offset fields).
static int32_t ltvBinaryInt(CPtr<ListreeValue> v) {
    if (!v || !v->getData() || v->getLength() < 4) return -1;
    if ((v->getFlags() & LtvFlags::Binary) == LtvFlags::None) return -1;
    int32_t val = 0;
    std::memcpy(&val, v->getData(), sizeof(val));
    return val;
}

// Canonicalize LP64 platform typedef aliases to their underlying C type.
// Only affects types that scalarSize() handles specially.
static std::string canonicalType(const std::string& t) {
    // 8-byte long-sized aliases (LP64 Linux x86-64)
    if (t == "__time_t" || t == "__clock_t" || t == "__syscall_slong_t" ||
        t == "__syscall_ulong_t" || t == "__blkcnt_t" || t == "__blkcnt64_t" ||
        t == "__off_t" || t == "__off64_t" || t == "__loff_t" ||
        t == "__ino_t" || t == "__ino64_t" || t == "__nlink_t" ||
        t == "__fsfilcnt_t" || t == "__fsfilcnt64_t" || t == "__fsblkcnt_t" ||
        t == "__fsblkcnt64_t" || t == "__pid_t" || t == "__uid_t" ||
        t == "__gid_t" || t == "__dev_t" ||
        t == "__suseconds_t" || t == "suseconds_t" ||
        t == "ssize_t" || t == "ptrdiff_t" || t == "intptr_t" ||
        t == "__intptr_t" || t == "int64_t" || t == "time_t" ||
        t == "clock_t" || t == "size_t") {
        return "long";
    }
    if (t == "__syscall_ulong_t" || t == "uint64_t" || t == "uintptr_t") {
        return "unsigned long";
    }
    // 4-byte int-sized aliases
    if (t == "__int32_t" || t == "__mode_t" || t == "__socklen_t" ||
        t == "__useconds_t" || t == "int32_t") {
        return "int";
    }
    if (t == "__uint32_t" || t == "uint32_t") {
        return "unsigned int";
    }
    if (t == "__int16_t" || t == "int16_t") return "short";
    if (t == "__uint16_t" || t == "uint16_t") return "unsigned short";
    if (t == "__int8_t"  || t == "int8_t")  return "char";
    if (t == "__uint8_t" || t == "uint8_t") return "unsigned char";
    return t; // already canonical
}

} // namespace

// ---------------------------------------------------------------------------
// Static scalar helpers
// ---------------------------------------------------------------------------

size_t Boxing::scalarSize(const std::string& rawT) {
    const std::string t = canonicalType(rawT);
    if (t == "char" || t == "unsigned char" || t == "signed char") return 1;
    if (t == "short" || t == "short int" || t == "unsigned short" || t == "unsigned short int") return 2;
    if (t == "int" || t == "unsigned int" || t == "signed int") return 4;
    if (t == "long" || t == "unsigned long" || t == "long int" || t == "unsigned long int") return 8;
    if (t == "long long" || t == "long long int" || t == "unsigned long long" || t == "unsigned long long int") return 8;
    if (t == "float") return 4;
    if (t == "double") return 8;
    if (t.find('*') != std::string::npos) return sizeof(void*); // pointer

    return 0; // unrecognised / struct (size from type-def)
}

void Boxing::packScalar(const std::string& rawT, CPtr<ListreeValue> val, void* dest) {
    const std::string t = canonicalType(rawT);
    // Get text representation from Edict value.
    std::string s;
    if (val && val->getData() && val->getLength() > 0) {
        bool isBin = (val->getFlags() & LtvFlags::Binary) != LtvFlags::None;
        if (!isBin) {
            s.assign(static_cast<const char*>(val->getData()), val->getLength());
        }
    }

    if (t == "char" || t == "signed char") {
        int8_t v = s.empty() ? 0 : static_cast<int8_t>(std::stoi(s));
        std::memcpy(dest, &v, 1);
    } else if (t == "unsigned char") {
        uint8_t v = s.empty() ? 0 : static_cast<uint8_t>(std::stoul(s));
        std::memcpy(dest, &v, 1);
    } else if (t == "short" || t == "short int") {
        int16_t v = s.empty() ? 0 : static_cast<int16_t>(std::stoi(s));
        std::memcpy(dest, &v, 2);
    } else if (t == "unsigned short" || t == "unsigned short int") {
        uint16_t v = s.empty() ? 0 : static_cast<uint16_t>(std::stoul(s));
        std::memcpy(dest, &v, 2);
    } else if (t == "int" || t == "signed int") {
        int32_t v = s.empty() ? 0 : static_cast<int32_t>(std::stoi(s));
        std::memcpy(dest, &v, 4);
    } else if (t == "unsigned int") {
        uint32_t v = s.empty() ? 0 : static_cast<uint32_t>(std::stoul(s));
        std::memcpy(dest, &v, 4);
    } else if (t == "long" || t == "long int") {
        int64_t v = s.empty() ? 0 : static_cast<int64_t>(std::stoll(s));
        std::memcpy(dest, &v, 8);
    } else if (t == "unsigned long" || t == "unsigned long int") {
        uint64_t v = s.empty() ? 0 : static_cast<uint64_t>(std::stoull(s));
        std::memcpy(dest, &v, 8);
    } else if (t == "long long" || t == "long long int") {
        int64_t v = s.empty() ? 0 : static_cast<int64_t>(std::stoll(s));
        std::memcpy(dest, &v, 8);
    } else if (t == "unsigned long long" || t == "unsigned long long int") {
        uint64_t v = s.empty() ? 0 : static_cast<uint64_t>(std::stoull(s));
        std::memcpy(dest, &v, 8);
    } else if (t == "float") {
        float v = s.empty() ? 0.0f : std::stof(s);
        std::memcpy(dest, &v, 4);
    } else if (t == "double") {
        double v = s.empty() ? 0.0 : std::stod(s);
        std::memcpy(dest, &v, 8);
    } else if (t.find('*') != std::string::npos) {
        // Pointer: accept 8-byte binary blobs (from FFI convertReturn) or zero.
        uintptr_t v = 0;
        if (val && val->getData() && val->getLength() == sizeof(void*) &&
            (val->getFlags() & LtvFlags::Binary) != LtvFlags::None) {
            std::memcpy(&v, val->getData(), sizeof(void*));
        }
        std::memcpy(dest, &v, sizeof(void*));
    }
    // unrecognised type: leave zeroed
}

CPtr<ListreeValue> Boxing::unpackScalar(const std::string& rawT, const void* src) {
    const std::string t = canonicalType(rawT);
    char buf[64];

    if (t == "char" || t == "signed char") {
        int8_t v; std::memcpy(&v, src, 1);
        std::snprintf(buf, sizeof(buf), "%d", (int)v);
        return createStringValue(std::string(buf));
    } else if (t == "unsigned char") {
        uint8_t v; std::memcpy(&v, src, 1);
        std::snprintf(buf, sizeof(buf), "%u", (unsigned)v);
        return createStringValue(std::string(buf));
    } else if (t == "short" || t == "short int") {
        int16_t v; std::memcpy(&v, src, 2);
        std::snprintf(buf, sizeof(buf), "%d", (int)v);
        return createStringValue(std::string(buf));
    } else if (t == "unsigned short" || t == "unsigned short int") {
        uint16_t v; std::memcpy(&v, src, 2);
        std::snprintf(buf, sizeof(buf), "%u", (unsigned)v);
        return createStringValue(std::string(buf));
    } else if (t == "int" || t == "signed int") {
        int32_t v; std::memcpy(&v, src, 4);
        std::snprintf(buf, sizeof(buf), "%d", v);
        return createStringValue(std::string(buf));
    } else if (t == "unsigned int") {
        uint32_t v; std::memcpy(&v, src, 4);
        std::snprintf(buf, sizeof(buf), "%u", v);
        return createStringValue(std::string(buf));
    } else if (t == "long" || t == "long int") {
        int64_t v; std::memcpy(&v, src, 8);
        std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
        return createStringValue(std::string(buf));
    } else if (t == "unsigned long" || t == "unsigned long int") {
        uint64_t v; std::memcpy(&v, src, 8);
        std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
        return createStringValue(std::string(buf));
    } else if (t == "long long" || t == "long long int") {
        int64_t v; std::memcpy(&v, src, 8);
        std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
        return createStringValue(std::string(buf));
    } else if (t == "unsigned long long" || t == "unsigned long long int") {
        uint64_t v; std::memcpy(&v, src, 8);
        std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
        return createStringValue(std::string(buf));
    } else if (t == "float") {
        float v; std::memcpy(&v, src, 4);
        std::snprintf(buf, sizeof(buf), "%.9g", (double)v);
        return createStringValue(std::string(buf));
    } else if (t == "double") {
        double v; std::memcpy(&v, src, 8);
        std::snprintf(buf, sizeof(buf), "%.15g", v);
        return createStringValue(std::string(buf));
    } else if (t.find('*') != std::string::npos) {
        // Pointer: return as 8-byte binary blob (consistent with FFI return).
        return createBinaryValue(const_cast<void*>(src), sizeof(void*));
    }
    return createNullValue();
}

// ---------------------------------------------------------------------------
// Recursive struct pack
// ---------------------------------------------------------------------------

bool Boxing::packStruct(CPtr<ListreeValue> source,
                        CPtr<ListreeValue> typeDef,
                        uint8_t* base) {
    if (!typeDef || !base) return false;

    auto childrenItem = typeDef->find("children");
    auto children = childrenItem ? childrenItem->getValue(false, false) : nullptr;
    if (!children) return true; // no children — nothing to pack

    bool ok = true;
    children->forEachTree([&](const std::string& fieldName, CPtr<ListreeItem>& item) {
        if (!ok) return;
        auto fieldDef = item ? item->getValue(false, false) : nullptr;
        if (!fieldDef) return;

        // Get field kind
        std::string kind = ltvString(fieldDef->find("kind") ? fieldDef->find("kind")->getValue(false, false) : nullptr);
        if (kind != "Field") return;

        // Get field type name
        std::string fieldType = ltvString(fieldDef->find("type") ? fieldDef->find("type")->getValue(false, false) : nullptr);

        // Get field byte offset (stored as 4-byte binary int)
        int32_t byteOffset = ltvBinaryInt(fieldDef->find("offset") ? fieldDef->find("offset")->getValue(false, false) : nullptr);
        if (byteOffset < 0) return; // can't place field without offset

        uint8_t* dest = base + byteOffset;

        size_t ss = scalarSize(fieldType);
        if (ss > 0) {
            // Scalar field
            auto srcItem = source ? source->find(fieldName) : nullptr;
            auto srcVal = srcItem ? srcItem->getValue(false, false) : nullptr;
            packScalar(fieldType, srcVal, dest);
        }
        // Nested struct fields are not handled here — they require a sub-typeDef
        // look-up from the root namespace. For now leave nested structs zeroed;
        // the caller can call box recursively for each nested struct field.
    });

    return ok;
}

// ---------------------------------------------------------------------------
// Recursive struct unpack
// ---------------------------------------------------------------------------

CPtr<ListreeValue> Boxing::unpackStruct(CPtr<ListreeValue> typeDef,
                                        const uint8_t* base) {
    auto result = createNullValue();
    if (!typeDef || !base) return result;

    auto childrenItem = typeDef->find("children");
    auto children = childrenItem ? childrenItem->getValue(false, false) : nullptr;
    if (!children) return result;

    children->forEachTree([&](const std::string& fieldName, CPtr<ListreeItem>& item) {
        auto fieldDef = item ? item->getValue(false, false) : nullptr;
        if (!fieldDef) return;

        std::string kind = ltvString(fieldDef->find("kind") ? fieldDef->find("kind")->getValue(false, false) : nullptr);
        if (kind != "Field") return;

        std::string fieldType = ltvString(fieldDef->find("type") ? fieldDef->find("type")->getValue(false, false) : nullptr);
        int32_t byteOffset = ltvBinaryInt(fieldDef->find("offset") ? fieldDef->find("offset")->getValue(false, false) : nullptr);
        if (byteOffset < 0) return;

        const uint8_t* src = base + byteOffset;

        size_t ss = scalarSize(fieldType);
        if (ss > 0) {
            addNamedItem(result, fieldName, unpackScalar(fieldType, src));
        }
        // nested structs: leave as null for now (same caveat as packStruct)
    });

    return result;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

CPtr<ListreeValue> Boxing::box(CPtr<ListreeValue> source, CPtr<ListreeValue> typeDef) const {
    if (!source || !typeDef) return nullptr;

    // Get total struct size from typeDef's "size" field (4-byte binary int).
    auto sizeItem = typeDef->find("size");
    int32_t structSize = ltvBinaryInt(sizeItem ? sizeItem->getValue(false, false) : nullptr);
    if (structSize <= 0) return nullptr;

    // Allocate zeroed heap buffer.
    uint8_t* buf = static_cast<uint8_t*>(std::calloc(1, static_cast<size_t>(structSize)));
    if (!buf) return nullptr;

    // Pack fields.
    if (!packStruct(source, typeDef, buf)) {
        std::free(buf);
        return nullptr;
    }

    // Build boxed LTV: { __ptr: <binary:8>, __type: typeDef }
    auto boxed = createNullValue();
    addNamedItem(boxed, "__ptr", createBinaryValue(static_cast<void*>(&buf), sizeof(void*)));
    addNamedItem(boxed, "__type", typeDef);
    return boxed;
}

CPtr<ListreeValue> Boxing::unbox(CPtr<ListreeValue> boxed) const {
    if (!boxed) return nullptr;

    // Extract __ptr
    auto ptrItem = boxed->find("__ptr");
    auto ptrVal = ptrItem ? ptrItem->getValue(false, false) : nullptr;
    if (!ptrVal || ptrVal->getLength() != sizeof(void*) ||
        (ptrVal->getFlags() & LtvFlags::Binary) == LtvFlags::None) {
        return nullptr;
    }
    void* rawPtr = nullptr;
    std::memcpy(&rawPtr, ptrVal->getData(), sizeof(void*));
    if (!rawPtr) return nullptr;

    // Extract __type
    auto typeItem = boxed->find("__type");
    auto typeDef = typeItem ? typeItem->getValue(false, false) : nullptr;
    if (!typeDef) return nullptr;

    // Unpack struct
    auto result = unpackStruct(typeDef, static_cast<const uint8_t*>(rawPtr));

    // Attach __type annotation
    addNamedItem(result, "__type", typeDef);
    return result;
}

CPtr<ListreeValue> Boxing::annotate(CPtr<ListreeValue> ltv, CPtr<ListreeValue> typeDef) const {
    if (!ltv || !typeDef) return nullptr;

    // Validate conformance: every Field in typeDef's children must exist in ltv.
    auto childrenItem = typeDef->find("children");
    auto children = childrenItem ? childrenItem->getValue(false, false) : nullptr;
    if (children) {
        bool conformant = true;
        children->forEachTree([&](const std::string& fieldName, CPtr<ListreeItem>& item) {
            if (!conformant) return;
            auto fieldDef = item ? item->getValue(false, false) : nullptr;
            if (!fieldDef) return;
            std::string kind = ltvString(fieldDef->find("kind") ? fieldDef->find("kind")->getValue(false, false) : nullptr);
            if (kind != "Field") return;
            // Check ltv has this field
            auto existing = ltv->find(fieldName);
            if (!existing || !existing->getValue(false, false)) {
                conformant = false;
            }
        });
        if (!conformant) return nullptr;
    }

    // Build annotated copy: deep-copy ltv, add __type.
    auto annotated = createNullValue();
    ltv->forEachTree([&](const std::string& key, CPtr<ListreeItem>& item) {
        if (!item) return;
        auto val = item->getValue(false, false);
        if (!val) return;
        addNamedItem(annotated, key, val);
    });
    addNamedItem(annotated, "__type", typeDef);
    return annotated;
}

void Boxing::freeBox(CPtr<ListreeValue> boxed) {
    if (!boxed) return;
    auto ptrItem = boxed->find("__ptr");
    auto ptrVal = ptrItem ? ptrItem->getValue(false, false) : nullptr;
    if (!ptrVal || ptrVal->getLength() != sizeof(void*) ||
        (ptrVal->getFlags() & LtvFlags::Binary) == LtvFlags::None) {
        return;
    }
    void* rawPtr = nullptr;
    std::memcpy(&rawPtr, ptrVal->getData(), sizeof(void*));
    if (rawPtr) std::free(rawPtr);
}

} // namespace cartographer
} // namespace agentc
