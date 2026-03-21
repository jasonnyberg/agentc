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

// G018 smoke tests:
//   A3 — minimal C API test (ltv_create_*, ltv_set_named, ltv_get_named,
//          ltv_foreach_named, ltv_pack_scalar, ltv_unpack_scalar)
//   B3 — C++ boundary utility round-trip (cptr_to_ltv, ltv_borrow, ltv_adopt)
//   C4 — boxing FFI smoke test (agentc_box, agentc_unbox, agentc_box_free)

#include <gtest/gtest.h>
#include "../ltv_api.h"
#include "../boxing_ffi.h"
#include <cstring>
#include <string>

using namespace agentc;

// ============================================================
// A3 — Basic C API tests
// ============================================================

TEST(LtvApiTest, CreateNull) {
    LTV v = ltv_create_null();
    ASSERT_NE(v, LTV_NULL);
    EXPECT_NE(ltv_flags(v) & LTV_FLAG_NULL, 0u);
    // null LTV has no data payload
    EXPECT_EQ(ltv_length(v), 0u);
    ltv_unref(v);
}

TEST(LtvApiTest, CreateString) {
    LTV v = ltv_create_string("hello", 5);
    ASSERT_NE(v, LTV_NULL);
    EXPECT_EQ(ltv_length(v), 5u);
    ASSERT_NE(ltv_data(v), nullptr);
    EXPECT_EQ(0, std::memcmp(ltv_data(v), "hello", 5));
    // string values must NOT have the binary flag set
    EXPECT_EQ(ltv_flags(v) & LTV_FLAG_BINARY, 0u);
    ltv_unref(v);
}

TEST(LtvApiTest, CreateBinary) {
    int32_t val = 0x12345678;
    LTV v = ltv_create_binary(&val, sizeof(val));
    ASSERT_NE(v, LTV_NULL);
    EXPECT_NE(ltv_flags(v) & LTV_FLAG_BINARY, 0u);
    EXPECT_EQ(ltv_length(v), sizeof(val));
    ASSERT_NE(ltv_data(v), nullptr);
    int32_t got = 0;
    std::memcpy(&got, ltv_data(v), sizeof(got));
    EXPECT_EQ(got, val);
    ltv_unref(v);
}

TEST(LtvApiTest, SetGetNamed) {
    LTV parent = ltv_create_null();
    ASSERT_NE(parent, LTV_NULL);

    LTV child = ltv_create_string("world", 5);
    ASSERT_NE(child, LTV_NULL);
    ltv_set_named(parent, "foo", child);
    ltv_unref(child);  // tree took its own reference

    // Retrieve as a borrowed reference
    LTV got = ltv_get_named(parent, "foo");
    ASSERT_NE(got, LTV_NULL);
    EXPECT_EQ(ltv_length(got), 5u);
    EXPECT_EQ(0, std::memcmp(ltv_data(got), "world", 5));

    // Missing key returns LTV_NULL
    LTV missing = ltv_get_named(parent, "nonexistent");
    EXPECT_EQ(missing, LTV_NULL);

    ltv_unref(parent);
}

TEST(LtvApiTest, ForeachNamed) {
    LTV parent = ltv_create_null();
    ASSERT_NE(parent, LTV_NULL);

    LTV a = ltv_create_string("alpha", 5);
    LTV b = ltv_create_string("beta",  4);
    ltv_set_named(parent, "a", a);
    ltv_set_named(parent, "b", b);
    ltv_unref(a);
    ltv_unref(b);

    int count = 0;
    ltv_foreach_named(parent,
        [](const char* /*name*/, LTV /*child*/, void* ud) {
            (*reinterpret_cast<int*>(ud))++;
        },
        &count);
    EXPECT_EQ(count, 2);

    ltv_unref(parent);
}

TEST(LtvApiTest, PackUnpackScalarInt) {
    // Pack: string "42" → int32 buffer
    LTV val = ltv_create_string("42", 2);
    ASSERT_NE(val, LTV_NULL);
    int32_t buf = 0;
    int rc = ltv_pack_scalar("int", val, &buf);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(buf, 42);
    ltv_unref(val);

    // Unpack: int32 buffer → string LTV
    LTV result = ltv_unpack_scalar("int", &buf);
    ASSERT_NE(result, LTV_NULL);
    std::string s(static_cast<const char*>(ltv_data(result)), ltv_length(result));
    EXPECT_EQ(s, "42");
    ltv_unref(result);
}

TEST(LtvApiTest, RefUnrefLifecycle) {
    // Create with refcount 1, bump to 2, then release twice — no crash or leak.
    LTV v = ltv_create_string("x", 1);
    ASSERT_NE(v, LTV_NULL);
    ltv_ref(v);    // refcount → 2
    ltv_unref(v);  // refcount → 1
    ltv_unref(v);  // refcount → 0, freed
    // No assertion beyond not crashing.
}

TEST(LtvApiTest, NullSafety) {
    // All API functions must be safe to call with LTV_NULL.
    ltv_ref(LTV_NULL);
    ltv_unref(LTV_NULL);
    EXPECT_EQ(ltv_flags(LTV_NULL),  LTV_FLAG_NULL);
    EXPECT_EQ(ltv_data(LTV_NULL),   nullptr);
    EXPECT_EQ(ltv_length(LTV_NULL), 0u);
    EXPECT_EQ(ltv_get_named(LTV_NULL, "x"), LTV_NULL);
    // Invalid non-null LTV (slab 0, slot 1) with null key — must not crash.
    EXPECT_EQ(ltv_get_named(LTV(0, 1), nullptr), LTV_NULL);
    ltv_set_named(LTV_NULL, "x", LTV_NULL);
    ltv_foreach_named(LTV_NULL, nullptr, nullptr);
    EXPECT_NE(ltv_pack_scalar("int", LTV_NULL, nullptr), 0);
    LTV nul = ltv_unpack_scalar(nullptr, nullptr);
    if (nul != LTV_NULL) ltv_unref(nul);
}

// ============================================================
// B3 — C++ boundary utility round-trip tests
// ============================================================

TEST(LtvBoundaryTest, CptrToLtv) {
    CPtr<ListreeValue> cptr = createNullValue();
    ASSERT_TRUE(bool(cptr));

    LTV ltv = cptr_to_ltv(cptr);
    EXPECT_NE(ltv, LTV_NULL);
    // Must refer to the same slab slot
    EXPECT_EQ(ltv, cptr.getSlabId());
}

TEST(LtvBoundaryTest, LtvBorrow) {
    CPtr<ListreeValue> original = createNullValue();
    ASSERT_TRUE(bool(original));

    LTV ltv = cptr_to_ltv(original);
    // ltv_borrow adds a shared reference — both CPtrs live simultaneously.
    CPtr<ListreeValue> shared = ltv_borrow(ltv);
    EXPECT_TRUE(bool(shared));
    EXPECT_EQ(shared.getSlabId(), original.getSlabId());
    // Both CPtrs destruct at scope exit; value freed when last one goes away.
}

TEST(LtvBoundaryTest, LtvAdopt) {
    // ltv_create_null() returns an owned LTV (refcount = 1).
    LTV ltv = ltv_create_null();
    ASSERT_NE(ltv, LTV_NULL);

    // ltv_adopt takes ownership WITHOUT an extra addref.
    // When the CPtr destructs, refcount drops from 1 → 0 and the value is freed.
    CPtr<ListreeValue> adopted = ltv_adopt(ltv);
    EXPECT_TRUE(bool(adopted));
    // Destructor of 'adopted' releases the value — no leak.
}

TEST(LtvBoundaryTest, RoundTrip) {
    // Create a CPtr, extract LTV, re-wrap with ltv_borrow, verify identity.
    CPtr<ListreeValue> original = createStringValue(std::string("test", 4));
    ASSERT_TRUE(bool(original));

    LTV ltv = cptr_to_ltv(original);
    CPtr<ListreeValue> copy = ltv_borrow(ltv);
    EXPECT_TRUE(bool(copy));

    // Both wrappers point to the same slab slot.
    EXPECT_EQ(original.getSlabId(), copy.getSlabId());

    // Data should be readable through the copy.
    EXPECT_EQ(copy->getLength(), 4u);
    EXPECT_EQ(0, std::memcmp(copy->getData(), "test", 4));
}

// ============================================================
// C4 — Boxing FFI smoke tests
// ============================================================

// Helper: build a minimal typeDef LTV representing:
//   struct { int x; }  (size = 4, one field: x at offset 0)
//
// Tree shape expected by boxing_ffi.c:
//   typeDef
//     "size"     → binary[4] = 4
//     "children"
//       "x"
//         "kind"   → string "Field"
//         "type"   → string "int"
//         "offset" → binary[4] = 0
static LTV make_int_struct_typedef() {
    LTV typeDef = ltv_create_null();
    if (typeDef == LTV_NULL) return LTV_NULL;

    // "size" = 4
    int32_t structSize = 4;
    LTV sizeVal = ltv_create_binary(&structSize, sizeof(structSize));
    ltv_set_named(typeDef, "size", sizeVal);
    ltv_unref(sizeVal);

    // "children" container
    LTV children = ltv_create_null();

    // Field "x"
    LTV fieldX = ltv_create_null();

    LTV kindVal = ltv_create_string("Field", 5);
    ltv_set_named(fieldX, "kind", kindVal);
    ltv_unref(kindVal);

    LTV typeVal = ltv_create_string("int", 3);
    ltv_set_named(fieldX, "type", typeVal);
    ltv_unref(typeVal);

    int32_t offset = 0;
    LTV offsetVal = ltv_create_binary(&offset, sizeof(offset));
    ltv_set_named(fieldX, "offset", offsetVal);
    ltv_unref(offsetVal);

    ltv_set_named(children, "x", fieldX);
    ltv_unref(fieldX);

    ltv_set_named(typeDef, "children", children);
    ltv_unref(children);

    return typeDef;  // owned
}

TEST(BoxingFFITest, BoxUnboxRoundTrip) {
    LTV typeDef = make_int_struct_typedef();
    ASSERT_NE(typeDef, LTV_NULL);

    // Source: { "x": "42" }
    LTV source = ltv_create_null();
    LTV xVal = ltv_create_string("42", 2);
    ltv_set_named(source, "x", xVal);
    ltv_unref(xVal);

    // --- Box ---
    LTV boxed = agentc_box(source, typeDef);
    ASSERT_NE(boxed, LTV_NULL) << "agentc_box returned LTV_NULL";

    // __ptr must be a binary blob of sizeof(void*) bytes
    LTV ptrField = ltv_get_named(boxed, "__ptr");
    ASSERT_NE(ptrField, LTV_NULL) << "boxed LTV has no __ptr field";
    EXPECT_NE(ltv_flags(ptrField) & LTV_FLAG_BINARY, 0u);
    EXPECT_EQ(ltv_length(ptrField), sizeof(void*));

    // __type must be present
    LTV typeField = ltv_get_named(boxed, "__type");
    EXPECT_NE(typeField, LTV_NULL) << "boxed LTV has no __type field";

    // --- Unbox ---
    LTV unboxed = agentc_unbox(boxed);
    ASSERT_NE(unboxed, LTV_NULL) << "agentc_unbox returned LTV_NULL";

    // The "x" field must round-trip as "42"
    LTV xResult = ltv_get_named(unboxed, "x");
    ASSERT_NE(xResult, LTV_NULL) << "unboxed LTV has no 'x' field";
    std::string xStr(static_cast<const char*>(ltv_data(xResult)), ltv_length(xResult));
    EXPECT_EQ(xStr, "42");

    // --- Free ---
    agentc_box_free(boxed);

    // Cleanup (order: unboxed first, then boxed, source, typeDef)
    ltv_unref(unboxed);
    ltv_unref(boxed);
    ltv_unref(source);
    ltv_unref(typeDef);
}

TEST(BoxingFFITest, BoxFreeNullSafe) {
    // agentc_box_free(LTV_NULL) must not crash.
    agentc_box_free(LTV_NULL);
}

TEST(BoxingFFITest, BoxNullTypeDefReturnsNull) {
    LTV source = ltv_create_null();
    ASSERT_NE(source, LTV_NULL);
    LTV result = agentc_box(source, LTV_NULL);
    EXPECT_EQ(result, LTV_NULL);
    ltv_unref(source);
}

TEST(BoxingFFITest, UnboxNullReturnsNull) {
    LTV result = agentc_unbox(LTV_NULL);
    EXPECT_EQ(result, LTV_NULL);
}
