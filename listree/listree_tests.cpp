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
