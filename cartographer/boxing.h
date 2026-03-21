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

#include <string>
#include "../core/container.h"
#include "../listree/listree.h"

namespace agentc {
namespace cartographer {

// Boxing/unboxing: universal bridge between Edict LTV world and C/C++ heap.
//
// A "boxed value" is an LTV dict:
//   { __ptr: <binary:8>, __type: <type-def-LTV> }
// where __ptr holds a malloc'd C struct and __type is the type-definition LTV
// produced by parser.__native.map (i.e. the struct's node in the flat namespace).
//
// An "unboxed value" is a field-keyed LTV (string or numeric values per field)
// with an optional __type annotation.
//
// Scalar type coverage (LP64):
//   "char"           -> int8_t  / signed char
//   "unsigned char"  -> uint8_t
//   "short"          -> int16_t
//   "unsigned short" -> uint16_t
//   "int"            -> int32_t
//   "unsigned int"   -> uint32_t
//   "long"           -> int64_t  (LP64)
//   "unsigned long"  -> uint64_t (LP64)
//   "long long"      -> int64_t
//   "unsigned long long" -> uint64_t
//   "float"          -> float (32-bit)
//   "double"         -> double (64-bit)
//   pointer (anything with '*') -> void* (uintptr_t, 8 bytes on LP64)
//   nested struct    -> recursive
//
// All Edict string representations are human-readable decimal / %g float
// strings, never opaque binary blobs.
class Boxing {
public:
    Boxing() = default;

    // box: allocate a C struct on the heap and pack fields from source LTV.
    //   source   — field-keyed LTV whose fields match typeDef's children
    //   typeDef  — the struct's type-definition node from the parser namespace
    // Returns { __ptr: <binary:8>, __type: typeDef } or nullptr on error.
    // The caller takes ownership of the heap allocation; use unbox then free
    // the pointer via the returned __ptr when done.
    CPtr<ListreeValue> box(CPtr<ListreeValue> source, CPtr<ListreeValue> typeDef) const;

    // unbox: read a C struct from a boxed value's __ptr and produce a
    //   field-keyed LTV.
    //   boxed — { __ptr: <binary:8>, __type: <type-def-LTV> }
    // Returns an LTV with one key per field (string values) plus __type.
    CPtr<ListreeValue> unbox(CPtr<ListreeValue> boxed) const;

    // annotate: attach __type to an existing LTV after validating conformance.
    //   ltv      — target LTV to annotate
    //   typeDef  — the struct's type-definition node
    // Returns a new LTV (copy with __type added) on success, nullptr if the
    // LTV's fields do not conform to typeDef's children.
    CPtr<ListreeValue> annotate(CPtr<ListreeValue> ltv, CPtr<ListreeValue> typeDef) const;

    // freeBox: free the heap allocation stored in a boxed value's __ptr.
    // Safe to call with nullptr.
    static void freeBox(CPtr<ListreeValue> boxed);

private:
    // Returns the byte size of a named C scalar type (or 0 for unrecognised).
    static size_t scalarSize(const std::string& typeName);

    // Pack a single scalar field from an Edict string value into raw storage.
    static void packScalar(const std::string& typeName,
                           CPtr<ListreeValue> val,
                           void* dest);

    // Unpack a single scalar field from raw storage into an Edict string value.
    static CPtr<ListreeValue> unpackScalar(const std::string& typeName,
                                           const void* src);

    // Recursive helpers used by box/unbox.
    static bool packStruct(CPtr<ListreeValue> source,
                           CPtr<ListreeValue> typeDef,
                           uint8_t* base);
    static CPtr<ListreeValue> unpackStruct(CPtr<ListreeValue> typeDef,
                                           const uint8_t* base);
};

} // namespace cartographer
} // namespace agentc
