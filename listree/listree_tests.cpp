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

#include <gtest/gtest.h>
#include "listree.h"
#include "../core/cursor.h"

using namespace agentc;

static std::string valueText(CPtr<ListreeValue> value) {
    if (!value || !value->getData()) {
        return "";
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

// Test basic ListreeValue creation
TEST(ListreeTest, BasicCreation) {
    // Create a null value
    CPtr<ListreeValue> nullValue = createNullValue();
    EXPECT_TRUE(nullValue);
    EXPECT_TRUE(nullValue->isEmpty());
    EXPECT_FALSE(nullValue->isListMode());
    
    // Create a list value
    CPtr<ListreeValue> listValue = createListValue();
    EXPECT_TRUE(listValue);
    EXPECT_TRUE(listValue->isEmpty());
    EXPECT_TRUE(listValue->isListMode());
    
    // Create a string value
    CPtr<ListreeValue> stringValue = createStringValue("Hello, world!");
    EXPECT_TRUE(stringValue);
    EXPECT_FALSE(stringValue->isEmpty());
    EXPECT_FALSE(stringValue->isListMode());
    EXPECT_EQ(stringValue->getLength(), 13);
    EXPECT_EQ(std::string(static_cast<const char*>(stringValue->getData()), stringValue->getLength()), "Hello, world!");
}

TEST(ListreeTest, AddNamedItemUsesLifoItemSemantics) {
    CPtr<ListreeValue> root = createNullValue();

    addNamedItem(root, "name", createStringValue("first"));
    addNamedItem(root, "name", createStringValue("second"));

    CPtr<ListreeItem> item = root->find("name");
    ASSERT_TRUE(item);

    EXPECT_EQ(valueText(item->getValue(false, false)), "second");
    EXPECT_EQ(valueText(item->getValue(false, true)), "first");
}

TEST(ListreeTest, CursorCreateUsesLifoItemSemantics) {
    CPtr<ListreeValue> root = createNullValue();
    Cursor first(root);
    Cursor second(root);
    Cursor newestReader(root);
    Cursor oldestReader(root);

    ASSERT_TRUE(first.create("name", createStringValue("first")));
    ASSERT_TRUE(second.create("name", createStringValue("second")));

    ASSERT_TRUE(newestReader.resolve("name"));
    EXPECT_EQ(valueText(newestReader.getValue()), "second");

    ASSERT_TRUE(oldestReader.resolve("-name"));
    EXPECT_EQ(valueText(oldestReader.getValue()), "first");
}

TEST(ListreeTest, ListreeItemAddValueDefaultsToLifoSemantics) {
    CPtr<ListreeItem> item("name");
    CPtr<ListreeValue> first = createStringValue("first");
    CPtr<ListreeValue> second = createStringValue("second");

    item->addValue(first);
    item->addValue(second);

    EXPECT_EQ(valueText(item->getValue(false, false)), "second");
    EXPECT_EQ(valueText(item->getValue(false, true)), "first");
}

TEST(ListreeTest, ListreeCopyPreservesItemValueOrder) {
    CPtr<ListreeValue> root = createNullValue();

    addNamedItem(root, "name", createStringValue("first"));
    addNamedItem(root, "name", createStringValue("second"));

    CPtr<ListreeValue> copy = root->copy();
    ASSERT_TRUE(copy);

    CPtr<ListreeItem> copiedItem = copy->find("name");
    ASSERT_TRUE(copiedItem);

    EXPECT_EQ(valueText(copiedItem->getValue(false, false)), "second");
    EXPECT_EQ(valueText(copiedItem->getValue(false, true)), "first");
}

TEST(ListreeTest, CursorComposedTailDereferenceUsesTailListFromOldestItemValue) {
    CPtr<ListreeValue> root = createNullValue();

    CPtr<ListreeValue> first = createListValue();
    first->put(createStringValue("old-a"));
    first->put(createStringValue("old-b"));

    CPtr<ListreeValue> second = createListValue();
    second->put(createStringValue("new-a"));
    second->put(createStringValue("new-b"));

    addNamedItem(root, "name", first);
    addNamedItem(root, "name", second);

    Cursor newestTail(root);
    Cursor oldestTail(root);

    ASSERT_TRUE(newestTail.resolve("name-"));
    EXPECT_EQ(valueText(newestTail.getValue()), "new-b");

    ASSERT_TRUE(oldestTail.resolve("-name-"));
    EXPECT_EQ(valueText(oldestTail.getValue()), "old-b");
}

TEST(ListreeTest, CursorComposedTailDereferenceRejectsNonListValue) {
    CPtr<ListreeValue> root = createNullValue();

    addNamedItem(root, "name", createStringValue("first"));
    addNamedItem(root, "name", createStringValue("second"));

    Cursor cursor(root);
    EXPECT_FALSE(cursor.resolve("-name-"));
    EXPECT_EQ(cursor.getLastError(), "Invalid list-tail dereference for '-name-'");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// ======================================================================
// G058 — ReadOnly branch tests
// ======================================================================

TEST(ReadOnlyTest, MutationGuardsFindInsert) {
    CPtr<ListreeValue> root = createNullValue();
    addNamedItem(root, "a", createStringValue("1"));
    root->setReadOnly(false);           // non-recursive freeze

    // Reads still work
    EXPECT_TRUE(bool(root->find("a")));

    // Structural insert is refused
    CPtr<ListreeItem> item = root->find("b", true);
    EXPECT_FALSE(bool(item));

    // Existing content unchanged
    auto existing = root->find("a");
    ASSERT_TRUE(bool(existing));
    auto val = existing->getValue(false, false);
    ASSERT_TRUE(bool(val));
    EXPECT_EQ(std::string(static_cast<char*>(val->getData()), val->getLength()), "1");
}

TEST(ReadOnlyTest, MutationGuardsRemove) {
    CPtr<ListreeValue> root = createNullValue();
    addNamedItem(root, "a", createStringValue("1"));
    root->setReadOnly(false);

    // Remove is refused
    CPtr<ListreeItem> removed = root->remove("a");
    EXPECT_FALSE(bool(removed));

    // Item still present
    EXPECT_TRUE(bool(root->find("a")));
}

TEST(ReadOnlyTest, MutationGuardsAddNamedItem) {
    CPtr<ListreeValue> root = createNullValue();
    root->setReadOnly(false);

    addNamedItem(root, "x", createStringValue("v"));
    // addNamedItem should silently refuse; "x" should not appear
    EXPECT_FALSE(bool(root->find("x")));
}

TEST(ReadOnlyTest, RecursiveFreezeMarksDescendants) {
    CPtr<ListreeValue> parent = createNullValue();
    CPtr<ListreeValue> child  = createNullValue();
    CPtr<ListreeValue> grandchild = createNullValue();

    addNamedItem(grandchild, "g", createStringValue("deep"));
    addNamedItem(child, "child", grandchild);
    addNamedItem(parent, "parent", child);

    parent->setReadOnly(true);      // recursive

    EXPECT_TRUE(parent->isReadOnly());
    EXPECT_TRUE(child->isReadOnly());
    EXPECT_TRUE(grandchild->isReadOnly());
}

TEST(ReadOnlyTest, RecursiveFreezeSKipsBinaryNodes) {
    // Binary nodes (bytecode/thunk frames) must NOT be frozen — the VM
    // writes .ip into them.
    const char data[] = {0x01, 0x02, 0x03};
    CPtr<ListreeValue> bin = createBinaryValue(data, sizeof(data));

    CPtr<ListreeValue> parent = createNullValue();
    addNamedItem(parent, "code", bin);

    parent->setReadOnly(true);      // recursive

    EXPECT_TRUE(parent->isReadOnly());
    // Binary child must remain mutable
    EXPECT_FALSE(bin->isReadOnly());
}

TEST(ReadOnlyTest, CopyOfReadOnlyNodeReturnsSameSlabId) {
    CPtr<ListreeValue> node = createNullValue();
    addNamedItem(node, "k", createStringValue("v"));
    node->setReadOnly(false);       // freeze just the root node

    SlabId original = node.getSlabId();
    CPtr<ListreeValue> copied = node->copy();

    // O(1) short-circuit: same slab slot returned
    EXPECT_EQ(copied.getSlabId(), original);
}

TEST(ReadOnlyTest, CopyOfMutableNodeAllocatesNew) {
    CPtr<ListreeValue> node = createNullValue();
    addNamedItem(node, "k", createStringValue("v"));
    // NOT frozen

    SlabId original = node.getSlabId();
    CPtr<ListreeValue> copied = node->copy();

    // Deep copy: different slab slot
    EXPECT_NE(copied.getSlabId(), original);
    // But content is the same
    auto item = copied->find("k");
    ASSERT_TRUE(bool(item));
    auto val = item->getValue(false, false);
    ASSERT_TRUE(bool(val));
    EXPECT_EQ(std::string(static_cast<char*>(val->getData()), val->getLength()), "v");
}

TEST(ReadOnlyTest, ReadOnlyFlagIsOneWay) {
    CPtr<ListreeValue> node = createNullValue();
    node->setReadOnly(false);
    EXPECT_TRUE(node->isReadOnly());

    // clearFlags must not clear ReadOnly
    node->clearFlags(LtvFlags::ReadOnly);
    EXPECT_TRUE(node->isReadOnly());
}

// ======================================================================
// toJson serialization tests
// ======================================================================

TEST(ToJsonTest, NullValueIsJsonNull) {
    auto v = createNullValue();
    EXPECT_EQ(toJson(v), "null");
}

TEST(ToJsonTest, NullptrIsJsonNull) {
    EXPECT_EQ(toJson(nullptr), "null");
}

TEST(ToJsonTest, StringLeafIsJsonString) {
    auto v = createStringValue("hello");
    EXPECT_EQ(toJson(v), "\"hello\"");
}

TEST(ToJsonTest, StringEscapesQuote) {
    auto v = createStringValue("say \"hi\"");
    EXPECT_EQ(toJson(v), "\"say \\\"hi\\\"\"");
}

TEST(ToJsonTest, StringEscapesBackslash) {
    auto v = createStringValue("a\\b");
    EXPECT_EQ(toJson(v), "\"a\\\\b\"");
}

TEST(ToJsonTest, StringEscapesNewline) {
    auto v = createStringValue("a\nb");
    EXPECT_EQ(toJson(v), "\"a\\nb\"");
}

TEST(ToJsonTest, StringEscapesControlChar) {
    auto v = createStringValue(std::string("a\x01 b"));
    EXPECT_EQ(toJson(v), "\"a\\u0001 b\"");
}

TEST(ToJsonTest, FlatDict) {
    auto v = createNullValue();
    addNamedItem(v, "name", createStringValue("alice"));
    addNamedItem(v, "age",  createStringValue("30"));
    // forEachTree visits in alphabetical order: age, name
    EXPECT_EQ(toJson(v), "{\"age\":\"30\",\"name\":\"alice\"}");
}

TEST(ToJsonTest, NestedDict) {
    auto inner = createNullValue();
    addNamedItem(inner, "role", createStringValue("admin"));

    auto outer = createNullValue();
    addNamedItem(outer, "meta", inner);
    addNamedItem(outer, "name", createStringValue("bob"));

    EXPECT_EQ(toJson(outer), "{\"meta\":{\"role\":\"admin\"},\"name\":\"bob\"}");
}

TEST(ToJsonTest, EmptyListIsJsonArray) {
    auto v = createListValue();
    EXPECT_EQ(toJson(v), "[]");
}

TEST(ToJsonTest, ListOfStrings) {
    auto v = createListValue();
    addListItem(v, createStringValue("x"));
    addListItem(v, createStringValue("y"));
    addListItem(v, createStringValue("z"));
    EXPECT_EQ(toJson(v), "[\"x\",\"y\",\"z\"]");
}

TEST(ToJsonTest, DictWithEmptyArrayValue) {
    auto tags = createListValue();
    auto v = createNullValue();
    addNamedItem(v, "tags", tags);
    EXPECT_EQ(toJson(v), "{\"tags\":[]}");
}

TEST(ToJsonTest, DictWithArrayValue) {
    auto arr = createListValue();
    addListItem(arr, createStringValue("a"));
    addListItem(arr, createStringValue("b"));

    auto v = createNullValue();
    addNamedItem(v, "items", arr);
    addNamedItem(v, "name",  createStringValue("test"));

    EXPECT_EQ(toJson(v), "{\"items\":[\"a\",\"b\"],\"name\":\"test\"}");
}

TEST(ToJsonTest, BinaryNodeIsJsonNull) {
    const char data[] = {0x01, 0x02, 0x03};
    auto bin = createBinaryValue(data, sizeof(data));
    EXPECT_EQ(toJson(bin), "null");
}

TEST(ToJsonTest, BinaryNodeSkippedInsideDict) {
    const char data[] = {0x01};
    auto bin = createBinaryValue(data, sizeof(data));
    auto v = createNullValue();
    addNamedItem(v, "code", bin);
    addNamedItem(v, "name", createStringValue("x"));
    // "code" maps to null (binary), "name" maps to "x"
    EXPECT_EQ(toJson(v), "{\"code\":null,\"name\":\"x\"}");
}

TEST(ToJsonTest, ReadOnlyNodeSerializesNormally) {
    auto v = createNullValue();
    addNamedItem(v, "k", createStringValue("v"));
    v->setReadOnly(true);
    EXPECT_EQ(toJson(v), "{\"k\":\"v\"}");
}

TEST(ToJsonTest, MultiValueItemUsesTopValue) {
    // ListreeItem can stack multiple values; to_json sees only the top
    auto v = createNullValue();
    addNamedItem(v, "k", createStringValue("first"));
    addNamedItem(v, "k", createStringValue("second"));  // second becomes top
    EXPECT_EQ(toJson(v), "{\"k\":\"second\"}");
}

TEST(ToJsonTest, EmptyStringLeafSerializesAsEmptyJsonString) {
    auto v = createStringValue("");
    EXPECT_EQ(toJson(v), "\"\"");
}

TEST(ToJsonTest, CheckEmptyStringFlags) {
    auto v = createStringValue("");
    EXPECT_EQ((v->getFlags() & LtvFlags::Null), LtvFlags::None);
    EXPECT_EQ(v->getLength(), 0);
}

// ---------------------------------------------------------------------------
// fromJson parsing tests
// ---------------------------------------------------------------------------

TEST(FromJsonTest, NullLiteralCreatesNullNode) {
    auto v = fromJson("null");
    ASSERT_TRUE(bool(v));
    EXPECT_TRUE((v->getFlags() & LtvFlags::Null) != LtvFlags::None);
}

TEST(FromJsonTest, EmptyStringCreatesNonNullStringNode) {
    // "" should produce a string node with no data, NOT a null node.
    auto v = fromJson("\"\"");
    ASSERT_TRUE(bool(v));
    EXPECT_TRUE((v->getFlags() & LtvFlags::Null) == LtvFlags::None);
    EXPECT_EQ(v->getLength(), 0u);
    EXPECT_EQ(toJson(v), "\"\"");
}

TEST(FromJsonTest, StringLiteralCreatesStringNode) {
    auto v = fromJson("\"hello\"");
    ASSERT_TRUE(bool(v));
    EXPECT_EQ(v->getLength(), 5u);
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "hello");
}

TEST(FromJsonTest, StringWithEscapeSequences) {
    // JSON: "a\nb" (backslash-n escape)
    auto v = fromJson("\"a\\nb\"");
    ASSERT_TRUE(bool(v));
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "a\nb");
}

TEST(FromJsonTest, StringWithQuoteEscape) {
    // JSON: "say \"hi\"" → say "hi"
    auto v = fromJson("\"say \\\"hi\\\"\"");
    ASSERT_TRUE(bool(v));
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "say \"hi\"");
}

TEST(FromJsonTest, StringWithBackslashEscape) {
    // JSON: "a\\b" → a\b
    auto v = fromJson("\"a\\\\b\"");
    ASSERT_TRUE(bool(v));
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "a\\b");
}

TEST(FromJsonTest, StringWithUnicodeEscape) {
    // JSON: "\u0041" → "A"
    auto v = fromJson("\"\\u0041\"");
    ASSERT_TRUE(bool(v));
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "A");
}

TEST(FromJsonTest, BooleanTrueCreatesStringTrue) {
    auto v = fromJson("true");
    ASSERT_TRUE(bool(v));
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "true");
}

TEST(FromJsonTest, BooleanFalseCreatesStringFalse) {
    auto v = fromJson("false");
    ASSERT_TRUE(bool(v));
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "false");
}

TEST(FromJsonTest, NumberLiteralCreatesStringRepresentation) {
    auto v = fromJson("42");
    ASSERT_TRUE(bool(v));
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "42");
}

TEST(FromJsonTest, FloatLiteralCreatesStringRepresentation) {
    auto v = fromJson("3.14");
    ASSERT_TRUE(bool(v));
    std::string data(static_cast<const char*>(v->getData()), v->getLength());
    EXPECT_EQ(data, "3.14");
}

TEST(FromJsonTest, ObjectCreatesNamedChildren) {
    auto v = fromJson("{\"k\":\"v\"}");
    ASSERT_TRUE(bool(v));
    auto child = v->find("k");
    ASSERT_TRUE(bool(child));
    auto val = child->getValue(false, false);
    ASSERT_TRUE(bool(val));
    std::string data(static_cast<const char*>(val->getData()), val->getLength());
    EXPECT_EQ(data, "v");
}

TEST(FromJsonTest, ArrayCreatesListNode) {
    auto v = fromJson("[\"x\",\"y\"]");
    ASSERT_TRUE(bool(v));
    EXPECT_TRUE(v->isListMode());
    size_t count = 0;
    v->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (ref) ++count;
    }, false);
    EXPECT_EQ(count, 2u);
}

TEST(FromJsonTest, EmptyArrayCreatesEmptyListNode) {
    auto v = fromJson("[]");
    ASSERT_TRUE(bool(v));
    EXPECT_TRUE(v->isListMode());
}

TEST(FromJsonTest, EmptyInputReturnsNullptr) {
    auto v = fromJson("");
    EXPECT_FALSE(bool(v));
}

TEST(FromJsonTest, TrailingGarbageReturnsNullptr) {
    auto v = fromJson("\"hello\" trailing");
    EXPECT_FALSE(bool(v));
}

TEST(FromJsonTest, MalformedObjectReturnsNullptr) {
    EXPECT_FALSE(bool(fromJson("{\"k\"}"))); // missing colon + value
    EXPECT_FALSE(bool(fromJson("{\"k\":}"))); // missing value
}

TEST(FromJsonTest, UnclosedStringReturnsNullptr) {
    auto v = fromJson("\"unterminated");
    EXPECT_FALSE(bool(v));
}

// ---------------------------------------------------------------------------
// JSON round-trip tests (toJson → fromJson → toJson)
// ---------------------------------------------------------------------------

TEST(RoundTripTest, NullNodeRoundTrips) {
    auto v = createNullValue();
    const std::string json = toJson(v);
    EXPECT_EQ(json, "null");
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    EXPECT_EQ(toJson(decoded), json);
}

TEST(RoundTripTest, EmptyStringNodeRoundTrips) {
    auto v = createStringValue("");
    const std::string json = toJson(v);
    EXPECT_EQ(json, "\"\"");
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    // Decoded should also be an empty string, not null.
    EXPECT_TRUE((decoded->getFlags() & LtvFlags::Null) == LtvFlags::None);
    EXPECT_EQ(decoded->getLength(), 0u);
    EXPECT_EQ(toJson(decoded), json);
}

TEST(RoundTripTest, StringNodeRoundTrips) {
    auto v = createStringValue("hello world");
    const std::string json = toJson(v);
    EXPECT_EQ(json, "\"hello world\"");
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    EXPECT_EQ(toJson(decoded), json);
}

TEST(RoundTripTest, StringWithSpecialCharsRoundTrips) {
    const std::string original = "say \"hi\" and c:\\path\nnewline\ttab";
    auto v = createStringValue(original);
    const std::string json = toJson(v);
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    std::string recovered(static_cast<const char*>(decoded->getData()), decoded->getLength());
    EXPECT_EQ(recovered, original);
}

TEST(RoundTripTest, StringWithControlCharRoundTrips) {
    const std::string original = std::string("a") + '\x01' + " b";
    auto v = createStringValue(original);
    const std::string json = toJson(v);
    // Control char should be \u00XX-escaped
    EXPECT_NE(json.find("\\u0001"), std::string::npos);
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    std::string recovered(static_cast<const char*>(decoded->getData()), decoded->getLength());
    EXPECT_EQ(recovered, original);
}

TEST(RoundTripTest, FlatObjectRoundTrips) {
    auto v = createNullValue();
    addNamedItem(v, "age", createStringValue("30"));
    addNamedItem(v, "name", createStringValue("alice"));
    const std::string json = toJson(v);
    EXPECT_EQ(json, "{\"age\":\"30\",\"name\":\"alice\"}");
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    EXPECT_EQ(toJson(decoded), json);
}

TEST(RoundTripTest, NestedObjectRoundTrips) {
    auto inner = createNullValue();
    addNamedItem(inner, "role", createStringValue("admin"));
    auto outer = createNullValue();
    addNamedItem(outer, "meta", inner);
    addNamedItem(outer, "name", createStringValue("bob"));
    const std::string json = toJson(outer);
    EXPECT_EQ(json, "{\"meta\":{\"role\":\"admin\"},\"name\":\"bob\"}");
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    EXPECT_EQ(toJson(decoded), json);
}

TEST(RoundTripTest, ArrayRoundTrips) {
    auto v = createListValue();
    addListItem(v, createStringValue("x"));
    addListItem(v, createStringValue("y"));
    addListItem(v, createStringValue("z"));
    const std::string json = toJson(v);
    EXPECT_EQ(json, "[\"x\",\"y\",\"z\"]");
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    EXPECT_EQ(toJson(decoded), json);
}

TEST(RoundTripTest, MixedObjectWithArrayValueRoundTrips) {
    auto tags = createListValue();
    addListItem(tags, createStringValue("a"));
    addListItem(tags, createStringValue("b"));
    auto v = createNullValue();
    addNamedItem(v, "name", createStringValue("test"));
    addNamedItem(v, "items", tags);
    const std::string json = toJson(v);
    EXPECT_EQ(json, "{\"items\":[\"a\",\"b\"],\"name\":\"test\"}");
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    EXPECT_EQ(toJson(decoded), json);
}

TEST(RoundTripTest, ObjectWithEmptyStringValueRoundTrips) {
    auto v = createNullValue();
    addNamedItem(v, "key", createStringValue(""));
    addNamedItem(v, "other", createStringValue("val"));
    const std::string json = toJson(v);
    EXPECT_EQ(json, "{\"key\":\"\",\"other\":\"val\"}");
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    EXPECT_EQ(toJson(decoded), json);
}

TEST(RoundTripTest, LongStringBeyondSSOThresholdRoundTrips) {
    // SSO threshold is 15 bytes; use a string longer than that.
    const std::string original = "this string is definitely longer than fifteen bytes";
    auto v = createStringValue(original);
    const std::string json = toJson(v);
    auto decoded = fromJson(json);
    ASSERT_TRUE(bool(decoded));
    std::string recovered(static_cast<const char*>(decoded->getData()), decoded->getLength());
    EXPECT_EQ(recovered, original);
}
