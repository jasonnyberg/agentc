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
#include "core/alloc.h"
#include "core/cursor.h"
#include "listree/listree.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace std;

namespace {

template <typename T>
void saveAllocatorImagesToStore(ArenaStore& store, Allocator<T>& allocator, std::vector<ArenaSlabImage>& imagesOut) {
    imagesOut = allocator.exportSlabImages();
    ASSERT_FALSE(imagesOut.empty());
    for (const auto& image : imagesOut) {
        ASSERT_TRUE(store.saveSlab(image));
    }
}

template <typename T>
void restoreAllocatorImagesFromStore(ArenaStore& store, Allocator<T>& allocator, const std::vector<ArenaSlabImage>& savedImages) {
    std::vector<ArenaSlabImage> restored;
    for (const auto& image : savedImages) {
        ArenaSlabImage loaded;
        ASSERT_TRUE(store.loadSlab(image.slabIndex, loaded));
        restored.push_back(std::move(loaded));
    }
    ASSERT_TRUE(allocator.restoreSlabImages(restored));
}

void resetStructuredListreeAllocators() {
    Allocator<agentc::ListreeValue>::getAllocator().resetForTests();
    Allocator<agentc::ListreeValueRef>::getAllocator().resetForTests();
    Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().resetForTests();
    Allocator<agentc::ListreeItem>::getAllocator().resetForTests();
    Allocator<AATree<agentc::ListreeItem>>::getAllocator().resetForTests();
}

ArenaRootState singleRootState(const std::string& name, SlabId valueSid) {
    ArenaRootState rootState;
    rootState.anchors.push_back(ArenaRootAnchor{name, valueSid});
    return rootState;
}

ArenaRootState makeRootState(std::initializer_list<ArenaRootAnchor> anchors) {
    ArenaRootState rootState;
    for (const auto& anchor : anchors) {
        rootState.anchors.push_back(anchor);
    }
    return rootState;
}

} // namespace

// Test fixture
class CPtrTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Any setup needed
    }
};

template <typename Store>
void roundTripSlabImagesThroughStore(Store& store) {
    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();

    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    std::vector<CPtr<int>> values;
    values.reserve(SLAB_SIZE + 8);
    std::vector<SlabId> ids;
    for (int i = 0; i < static_cast<int>(SLAB_SIZE) + 8; ++i) {
        values.emplace_back(i + 1000);
        ids.push_back(values.back().getSlabId());
    }

    auto metadata = allocator.exportArenaMetadata();
    auto images = allocator.exportSlabImages();
    ASSERT_GE(images.size(), 2u);
    ASSERT_TRUE(store.saveNamedCheckpoint("bootstrap", metadata));
    for (const auto& image : images) {
        ASSERT_TRUE(store.saveSlab(image));
    }

    allocator.resetForTests();

    ArenaCheckpointMetadata loadedMetadata;
    ASSERT_TRUE(store.loadNamedCheckpoint("bootstrap", loadedMetadata));
    ASSERT_TRUE(allocator.restoreArenaMetadata(loadedMetadata));

    std::vector<ArenaSlabImage> restoredImages;
    for (const auto& image : images) {
        ArenaSlabImage restored;
        ASSERT_TRUE(store.loadSlab(image.slabIndex, restored));
        restoredImages.push_back(std::move(restored));
    }
    ASSERT_TRUE(allocator.restoreSlabImages(restoredImages));

    EXPECT_EQ(*CPtr<int>(ids.front()), 1000);
    EXPECT_EQ(*CPtr<int>(ids.back()), 1000 + static_cast<int>(SLAB_SIZE) + 7);

    auto revived = allocator.currentCheckpoint();
    ASSERT_TRUE(revived.valid);
    CPtr<int> transient(7777);
    SlabId transientSid = transient.getSlabId();
    ASSERT_TRUE(allocator.rollback(revived));
    EXPECT_FALSE(allocator.valid(transientSid));
}

TEST(CPtrTest, BasicAllocation) {
    CPtr<int> ptr(42);
    EXPECT_TRUE(bool(ptr));
    EXPECT_EQ(*ptr, 42);
}

TEST(CPtrTest, CopyAndRefCount) {
    CPtr<int> ptr1(100);
    EXPECT_EQ(ptr1.refs(), 1);
    
    {
        CPtr<int> ptr2 = ptr1;
        EXPECT_EQ(ptr1.refs(), 2);
        EXPECT_EQ(ptr2.refs(), 2);
        EXPECT_EQ(*ptr2, 100);
    }
    
    EXPECT_EQ(ptr1.refs(), 1);
}

TEST(CPtrTest, AssignmentAndRefCount) {
    CPtr<int> ptr1(10);
    CPtr<int> ptr2(20);
    
    EXPECT_EQ(ptr1.refs(), 1);
    EXPECT_EQ(ptr2.refs(), 1);
    
    ptr2 = ptr1;
    
    EXPECT_EQ(ptr1.refs(), 2);
    EXPECT_EQ(ptr2.refs(), 2);
    EXPECT_EQ(*ptr2, 10);
    
    // ptr2's original value (20) should have been deallocated (ref 0)
}

TEST(CPtrTest, Dereference) {
    struct TestStruct {
        int x;
        int y;
        TestStruct(int a, int b) : x(a), y(b) {}
    };
    
    CPtr<TestStruct> ptr(1, 2);
    EXPECT_EQ(ptr->x, 1);
    EXPECT_EQ(ptr->y, 2);
    EXPECT_EQ((*ptr).x, 1);
}

TEST(CPtrTest, ArrowOperator) {
    std::string val = "test";
    CPtr<std::string> ptr(val);
    EXPECT_EQ(ptr->length(), 4);
    EXPECT_EQ(*ptr, "test");
}

TEST(CPtrTest, BoolConversion) {
    CPtr<int> ptr1;
    EXPECT_FALSE(bool(ptr1));
    
    CPtr<int> ptr2(5);
    EXPECT_TRUE(bool(ptr2));
}

TEST(CPtrTest, MoveSemantics) {
    CPtr<int> ptr1(123);
    EXPECT_EQ(ptr1.refs(), 1);
    
    CPtr<int> ptr2 = std::move(ptr1);
    EXPECT_EQ(ptr2.refs(), 1);
    EXPECT_EQ(*ptr2, 123);
    EXPECT_FALSE(bool(ptr1)); // Moved from
}

TEST(CPtrTest, CheckpointRollbackInvalidatesTransientAllocation) {
    auto& allocator = Allocator<int>::getAllocator();
    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    CPtr<int> transient(77);
    SlabId sid = transient.getSlabId();
    EXPECT_TRUE(allocator.valid(sid));

    ASSERT_TRUE(allocator.rollback(checkpoint));
    EXPECT_FALSE(allocator.valid(sid));
    EXPECT_FALSE(bool(transient));
}

TEST(CPtrTest, NestedCheckpointRollbackPreservesOuterAllocation) {
    auto& allocator = Allocator<int>::getAllocator();
    auto outer = allocator.checkpoint();
    ASSERT_TRUE(outer.valid);

    CPtr<int> outerValue(10);
    SlabId outerSid = outerValue.getSlabId();

    auto inner = allocator.checkpoint();
    ASSERT_TRUE(inner.valid);

    CPtr<int> innerValue(20);
    SlabId innerSid = innerValue.getSlabId();
    ASSERT_TRUE(allocator.valid(outerSid));
    ASSERT_TRUE(allocator.valid(innerSid));

    ASSERT_TRUE(allocator.rollback(inner));
    EXPECT_TRUE(allocator.valid(outerSid));
    EXPECT_FALSE(allocator.valid(innerSid));

    ASSERT_TRUE(allocator.commit(outer));
    EXPECT_TRUE(allocator.valid(outerSid));
}

TEST(CPtrTest, NestedCheckpointCommitRequiresTopMostOrder) {
    auto& allocator = Allocator<int>::getAllocator();
    auto outer = allocator.checkpoint();
    ASSERT_TRUE(outer.valid);

    auto inner = allocator.checkpoint();
    ASSERT_TRUE(inner.valid);

    EXPECT_FALSE(allocator.commit(outer));

    ASSERT_TRUE(allocator.commit(inner));
    ASSERT_TRUE(allocator.commit(outer));
}

TEST(CPtrTest, FreedSlotIsNotReusedWhileCheckpointStackIsActive) {
    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();

    CPtr<int> baseline(10);
    SlabId baselineSid = baseline.getSlabId();

    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    baseline = CPtr<int>();
    EXPECT_FALSE(allocator.valid(baselineSid));

    CPtr<int> replacement(20);
    SlabId replacementSid = replacement.getSlabId();
    EXPECT_NE(replacementSid, baselineSid);

    ASSERT_TRUE(allocator.rollback(checkpoint));
    EXPECT_FALSE(allocator.valid(replacementSid));

    CPtr<int> afterRollback(30);
    EXPECT_EQ(afterRollback.getSlabId(), baselineSid);
}

TEST(CPtrTest, AppendOnlyCheckpointUsesWatermarkFastPathOnRollback) {
    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();

    CPtr<int> baseline(10);
    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);
    EXPECT_TRUE(checkpoint.appendOnlyEligible);

    CPtr<int> first(20);
    CPtr<int> second(30);
    EXPECT_GT(first.getSlabId(), baseline.getSlabId());
    EXPECT_GT(second.getSlabId(), first.getSlabId());

    ASSERT_TRUE(allocator.rollback(checkpoint));
    EXPECT_TRUE(allocator.lastRollbackUsedWatermarkFastPath());
    EXPECT_TRUE(allocator.lastRollbackUsedStrictWatermarkFastPath());
    EXPECT_FALSE(allocator.valid(first.getSlabId()));
    EXPECT_FALSE(allocator.valid(second.getSlabId()));
    EXPECT_TRUE(allocator.valid(baseline.getSlabId()));
}

TEST(CPtrTest, DeallocationInsideCheckpointFallsBackFromWatermarkFastPath) {
    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();

    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    {
        CPtr<int> doomed(10);
    }
    CPtr<int> replacement(20);

    auto current = allocator.currentCheckpoint();
    EXPECT_FALSE(current.appendOnlyEligible);

    ASSERT_TRUE(allocator.rollback(checkpoint));
    EXPECT_FALSE(allocator.lastRollbackUsedWatermarkFastPath());
    EXPECT_FALSE(allocator.lastRollbackUsedStrictWatermarkFastPath());
    EXPECT_FALSE(allocator.valid(replacement.getSlabId()));
}

TEST(CPtrTest, ReallocationReusesRolledBackSlot) {
    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();
    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    CPtr<int> transient(55);
    SlabId transientSid = transient.getSlabId();
    {
        CPtr<int> doomed(99);
    }

    EXPECT_FALSE(allocator.currentCheckpoint().appendOnlyEligible);

    ASSERT_TRUE(allocator.rollback(checkpoint));
    EXPECT_FALSE(allocator.lastRollbackUsedWatermarkFastPath());
    EXPECT_FALSE(allocator.lastRollbackUsedStrictWatermarkFastPath());

    CPtr<int> replacement(66);
    EXPECT_EQ(replacement.getSlabId(), transientSid);
}

TEST(CPtrTest, ListreeValueUsesStrictWatermarkPathWhenEligible) {
    resetStructuredListreeAllocators();

    auto& allocator = Allocator<agentc::ListreeValue>::getAllocator();
    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    auto created = agentc::createStringValue("watermark");
    SlabId createdSid = created.getSlabId();

    ASSERT_TRUE(allocator.rollback(checkpoint));
    EXPECT_TRUE(allocator.lastRollbackUsedWatermarkFastPath());
    EXPECT_TRUE(allocator.lastRollbackUsedStrictWatermarkFastPath());
    EXPECT_FALSE(allocator.valid(createdSid));

    resetStructuredListreeAllocators();
}

TEST(CPtrTest, ListreeValueRefUsesStrictWatermarkPathWhenEligible) {
    resetStructuredListreeAllocators();

    auto baseline = agentc::createStringValue("baseline");

    auto& refAllocator = Allocator<agentc::ListreeValueRef>::getAllocator();
    auto checkpoint = refAllocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    CPtr<agentc::ListreeValueRef> transientRef(baseline);
    SlabId transientSid = transientRef.getSlabId();

    ASSERT_TRUE(refAllocator.rollback(checkpoint));
    EXPECT_TRUE(refAllocator.lastRollbackUsedWatermarkFastPath());
    EXPECT_TRUE(refAllocator.lastRollbackUsedStrictWatermarkFastPath());
    EXPECT_FALSE(refAllocator.valid(transientSid));

    resetStructuredListreeAllocators();
}

TEST(CPtrTest, MemoryArenaStoreRoundTripsMetadata) {
    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();

    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    MemoryArenaStore store;
    auto metadata = allocator.exportArenaMetadata();
    ASSERT_TRUE(store.saveCurrent(metadata));

    ArenaCheckpointMetadata loaded;
    ASSERT_TRUE(store.loadCurrent(loaded));
    EXPECT_EQ(loaded.checkpointLogStarts.size(), 1u);
    EXPECT_EQ(loaded.checkpointLogStarts[0], 0u);

    allocator.resetForTests();
    ASSERT_TRUE(allocator.restoreArenaMetadata(loaded));

    auto revived = allocator.currentCheckpoint();
    ASSERT_TRUE(revived.valid);

    CPtr<int> transient(88);
    SlabId sid = transient.getSlabId();
    ASSERT_TRUE(allocator.rollback(revived));
    EXPECT_FALSE(allocator.valid(sid));
}

TEST(CPtrTest, FileArenaStoreRoundTripsNamedCheckpoint) {
    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();

    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);

    const std::string path = "/tmp/j3_arena_store_test.txt";
    std::remove(path.c_str());
    FileArenaStore store(path);
    ASSERT_TRUE(store.saveNamedCheckpoint("bootstrap", allocator.exportArenaMetadata()));

    allocator.resetForTests();

    ArenaCheckpointMetadata loaded;
    ASSERT_TRUE(store.loadNamedCheckpoint("bootstrap", loaded));
    ASSERT_TRUE(allocator.restoreArenaMetadata(loaded));

    auto revived = allocator.currentCheckpoint();
    ASSERT_TRUE(revived.valid);

    CPtr<int> transient(99);
    SlabId sid = transient.getSlabId();
    ASSERT_TRUE(allocator.rollback(revived));
    EXPECT_FALSE(allocator.valid(sid));

    std::remove(path.c_str());
}

TEST(CPtrTest, LmdbArenaStoreRoundTripsNamedCheckpoint) {
    const std::string path = "/tmp/j3_lmdb_arena_store_test";
    std::filesystem::remove_all(path);

    LmdbArenaStore store(path);
    ASSERT_TRUE(store.isAvailable());

    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();

    auto checkpoint = allocator.checkpoint();
    ASSERT_TRUE(checkpoint.valid);
    ASSERT_TRUE(store.saveCurrent(allocator.exportArenaMetadata()));
    ASSERT_TRUE(store.saveNamedCheckpoint("bootstrap", allocator.exportArenaMetadata()));

    allocator.resetForTests();

    ArenaCheckpointMetadata loaded;
    ASSERT_TRUE(store.loadCurrent(loaded));
    ASSERT_TRUE(store.loadNamedCheckpoint("bootstrap", loaded));
    ASSERT_TRUE(allocator.restoreArenaMetadata(loaded));

    auto revived = allocator.currentCheckpoint();
    ASSERT_TRUE(revived.valid);

    CPtr<int> transient(111);
    SlabId sid = transient.getSlabId();
    ASSERT_TRUE(allocator.rollback(revived));
    EXPECT_FALSE(allocator.valid(sid));

    std::filesystem::remove_all(path);
}

TEST(CPtrTest, MemoryArenaStoreRoundTripsSlabImages) {
    MemoryArenaStore store;
    roundTripSlabImagesThroughStore(store);
}

TEST(CPtrTest, FileArenaStoreRoundTripsSlabImages) {
    const std::string path = "/tmp/j3_arena_slab_store_test.txt";
    std::remove(path.c_str());
    FileArenaStore store(path);
    roundTripSlabImagesThroughStore(store);
    std::remove(path.c_str());
    for (int i = 0; i < 4; ++i) {
        std::remove((path + ".slab." + std::to_string(i)).c_str());
    }
}

TEST(CPtrTest, LmdbArenaStoreRoundTripsSlabImages) {
    const std::string path = "/tmp/j3_lmdb_arena_slab_store_test";
    std::filesystem::remove_all(path);
    LmdbArenaStore store(path);
    ASSERT_TRUE(store.isAvailable());
    roundTripSlabImagesThroughStore(store);
    std::filesystem::remove_all(path);
}

TEST(CPtrTest, FileArenaStoreRoundTripsStructuredListreeItemGraph) {
    resetStructuredListreeAllocators();

    auto value = agentc::createStringValue("persisted-value");
    CPtr<agentc::ListreeItem> item("topic");
    item->addValue(value, false);

    const SlabId itemSid = item.getSlabId();

    const std::string base = "/tmp/j3_structured_listree_store_test";
    std::remove((base + ".value").c_str());
    std::remove((base + ".ref").c_str());
    std::remove((base + ".node").c_str());
    std::remove((base + ".item").c_str());

    FileArenaStore valueStore(base + ".value");
    FileArenaStore refStore(base + ".ref");
    FileArenaStore nodeStore(base + ".node");
    FileArenaStore itemStore(base + ".item");

    std::vector<ArenaSlabImage> valueImages;
    std::vector<ArenaSlabImage> refImages;
    std::vector<ArenaSlabImage> nodeImages;
    std::vector<ArenaSlabImage> itemImages;
    saveAllocatorImagesToStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages);
    saveAllocatorImagesToStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages);
    saveAllocatorImagesToStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages);
    saveAllocatorImagesToStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages);

    resetStructuredListreeAllocators();

    restoreAllocatorImagesFromStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages);
    restoreAllocatorImagesFromStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages);
    restoreAllocatorImagesFromStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages);
    restoreAllocatorImagesFromStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages);

    CPtr<agentc::ListreeItem> restoredItem(itemSid);
    ASSERT_TRUE(restoredItem);
    EXPECT_EQ(restoredItem->getName(), "topic");

    auto restoredValue = restoredItem->getValue();
    ASSERT_TRUE(restoredValue);
    EXPECT_EQ(restoredValue->getLength(), std::string("persisted-value").size());
    EXPECT_EQ(std::string(static_cast<const char*>(restoredValue->getData()), restoredValue->getLength()), "persisted-value");

    resetStructuredListreeAllocators();
    std::remove((base + ".value").c_str());
    std::remove((base + ".ref").c_str());
    std::remove((base + ".node").c_str());
    std::remove((base + ".item").c_str());
    for (int i = 0; i < 4; ++i) {
        std::remove((base + ".value.slab." + std::to_string(i)).c_str());
        std::remove((base + ".ref.slab." + std::to_string(i)).c_str());
        std::remove((base + ".node.slab." + std::to_string(i)).c_str());
        std::remove((base + ".item.slab." + std::to_string(i)).c_str());
    }
}

TEST(CPtrTest, FileArenaStoreRoundTripsTopLevelTreeBackedListreeState) {
    resetStructuredListreeAllocators();

    auto root = agentc::createNullValue();
    auto title = agentc::createStringValue("root-title");
    auto child = agentc::createNullValue();
    auto childLeaf = agentc::createStringValue("nested-value");
    auto list = agentc::createListValue();
    auto listFirst = agentc::createStringValue("first");
    auto listSecond = agentc::createStringValue("second");

    agentc::addNamedItem(child, "leaf", childLeaf);
    agentc::addListItem(list, listFirst);
    agentc::addListItem(list, listSecond);
    agentc::addNamedItem(root, "title", title);
    agentc::addNamedItem(root, "child", child);
    agentc::addNamedItem(root, "list", list);

    const SlabId rootSid = root.getSlabId();

    const std::string base = "/tmp/j3_top_level_tree_restore_test";
    std::remove((base + ".value").c_str());
    std::remove((base + ".ref").c_str());
    std::remove((base + ".node").c_str());
    std::remove((base + ".item").c_str());
    std::remove((base + ".tree").c_str());

    FileArenaStore valueStore(base + ".value");
    FileArenaStore refStore(base + ".ref");
    FileArenaStore nodeStore(base + ".node");
    FileArenaStore itemStore(base + ".item");
    FileArenaStore treeStore(base + ".tree");

    std::vector<ArenaSlabImage> valueImages;
    std::vector<ArenaSlabImage> refImages;
    std::vector<ArenaSlabImage> nodeImages;
    std::vector<ArenaSlabImage> itemImages;
    std::vector<ArenaSlabImage> treeImages;
    saveAllocatorImagesToStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages);
    saveAllocatorImagesToStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages);
    saveAllocatorImagesToStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages);
    saveAllocatorImagesToStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages);
    saveAllocatorImagesToStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), treeImages);

    resetStructuredListreeAllocators();

    restoreAllocatorImagesFromStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages);
    restoreAllocatorImagesFromStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages);
    restoreAllocatorImagesFromStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages);
    restoreAllocatorImagesFromStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages);
    restoreAllocatorImagesFromStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), treeImages);

    CPtr<agentc::ListreeValue> restoredRoot(rootSid);
    ASSERT_TRUE(restoredRoot);

    std::vector<std::string> names;
    restoredRoot->forEachTree([&](const std::string& name, CPtr<agentc::ListreeItem>&) {
        names.push_back(name);
    });
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "child");
    EXPECT_EQ(names[1], "list");
    EXPECT_EQ(names[2], "title");

    auto restoredTitleItem = restoredRoot->find("title");
    ASSERT_TRUE(restoredTitleItem);
    auto restoredTitle = restoredTitleItem->getValue();
    ASSERT_TRUE(restoredTitle);
    EXPECT_EQ(std::string(static_cast<const char*>(restoredTitle->getData()), restoredTitle->getLength()), "root-title");

    auto restoredChildItem = restoredRoot->find("child");
    ASSERT_TRUE(restoredChildItem);
    auto restoredChild = restoredChildItem->getValue();
    ASSERT_TRUE(restoredChild);
    auto restoredLeafItem = restoredChild->find("leaf");
    ASSERT_TRUE(restoredLeafItem);
    auto restoredLeaf = restoredLeafItem->getValue();
    ASSERT_TRUE(restoredLeaf);
    EXPECT_EQ(std::string(static_cast<const char*>(restoredLeaf->getData()), restoredLeaf->getLength()), "nested-value");

    auto restoredListItem = restoredRoot->find("list");
    ASSERT_TRUE(restoredListItem);
    auto restoredList = restoredListItem->getValue();
    ASSERT_TRUE(restoredList);
    ASSERT_TRUE(restoredList->isListMode());
    auto restoredFirst = restoredList->get();
    ASSERT_TRUE(restoredFirst);
    auto restoredSecond = restoredList->get(false, true);
    ASSERT_TRUE(restoredSecond);
    EXPECT_EQ(std::string(static_cast<const char*>(restoredFirst->getData()), restoredFirst->getLength()), "first");
    EXPECT_EQ(std::string(static_cast<const char*>(restoredSecond->getData()), restoredSecond->getLength()), "second");

    resetStructuredListreeAllocators();
    std::remove((base + ".value").c_str());
    std::remove((base + ".ref").c_str());
    std::remove((base + ".node").c_str());
    std::remove((base + ".item").c_str());
    std::remove((base + ".tree").c_str());
    for (int i = 0; i < 4; ++i) {
        std::remove((base + ".value.slab." + std::to_string(i)).c_str());
        std::remove((base + ".ref.slab." + std::to_string(i)).c_str());
        std::remove((base + ".node.slab." + std::to_string(i)).c_str());
        std::remove((base + ".item.slab." + std::to_string(i)).c_str());
        std::remove((base + ".tree.slab." + std::to_string(i)).c_str());
    }
}

TEST(CPtrTest, ListreeValueSlabExportRejectsIteratorPayloads) {
    resetStructuredListreeAllocators();

    auto cursorValue = agentc::createCursorValue(new agentc::Cursor());
    ASSERT_TRUE(cursorValue);

    auto images = Allocator<agentc::ListreeValue>::getAllocator().exportSlabImages();
    EXPECT_TRUE(images.empty());

    resetStructuredListreeAllocators();
}

TEST(CPtrTest, ListreeValueSlabExportRejectsBorrowedPointerPayloads) {
    resetStructuredListreeAllocators();

    static const char borrowedBytes[] = "borrowed";
    SlabId sid = Allocator<agentc::ListreeValue>::getAllocator().allocate(
        const_cast<char*>(borrowedBytes),
        sizeof(borrowedBytes) - 1,
        agentc::LtvFlags::None);
    CPtr<agentc::ListreeValue> borrowedValue(sid);
    ASSERT_TRUE(borrowedValue);

    auto images = Allocator<agentc::ListreeValue>::getAllocator().exportSlabImages();
    EXPECT_TRUE(images.empty());

    resetStructuredListreeAllocators();
}

TEST(CPtrTest, ListreeValueSlabExportAllowsOwnedBinaryPayloads) {
    resetStructuredListreeAllocators();

    const int payload = 4242;
    auto binaryValue = agentc::createBinaryValue(&payload, sizeof(payload));
    ASSERT_TRUE(binaryValue);

    auto images = Allocator<agentc::ListreeValue>::getAllocator().exportSlabImages();
    ASSERT_EQ(images.size(), 1u);
    EXPECT_FALSE(images[0].structuredSlots.empty());

    resetStructuredListreeAllocators();
}

template <typename Store>
void roundTripAnchoredTreeStateThroughStores(Store& valueStore,
                                            Store& refStore,
                                            Store& nodeStore,
                                            Store& itemStore,
                                            Store& treeStore,
                                            Store& stateStore) {
    resetStructuredListreeAllocators();

    auto root = agentc::createNullValue();
    auto title = agentc::createStringValue("anchored-root");
    auto child = agentc::createNullValue();
    auto childLeaf = agentc::createStringValue("anchored-leaf");
    agentc::addNamedItem(child, "leaf", childLeaf);
    agentc::addNamedItem(root, "title", title);
    agentc::addNamedItem(root, "child", child);

    const SlabId rootSid = root.getSlabId();
    ASSERT_TRUE(stateStore.saveNamedCheckpoint("bootstrap", Allocator<agentc::ListreeValue>::getAllocator().exportArenaMetadata()));

    std::vector<ArenaSlabImage> valueImages;
    std::vector<ArenaSlabImage> refImages;
    std::vector<ArenaSlabImage> nodeImages;
    std::vector<ArenaSlabImage> itemImages;
    std::vector<ArenaSlabImage> treeImages;
    saveAllocatorImagesToStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages);
    saveAllocatorImagesToStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages);
    saveAllocatorImagesToStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages);
    saveAllocatorImagesToStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages);
    saveAllocatorImagesToStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), treeImages);
    ASSERT_TRUE(stateStore.saveRootState("bootstrap", singleRootState("root", rootSid)));

    resetStructuredListreeAllocators();

    ArenaCheckpointMetadata metadata;
    ASSERT_TRUE(stateStore.loadNamedCheckpoint("bootstrap", metadata));
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().restoreArenaMetadata(metadata));
    restoreAllocatorImagesFromStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages);
    restoreAllocatorImagesFromStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages);
    restoreAllocatorImagesFromStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages);
    restoreAllocatorImagesFromStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages);
    restoreAllocatorImagesFromStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), treeImages);

    ArenaRootState restoredRootState;
    ASSERT_TRUE(stateStore.loadRootState("bootstrap", restoredRootState));
    ASSERT_EQ(restoredRootState.anchors.size(), 1u);
    EXPECT_EQ(restoredRootState.anchors[0].name, "root");

    CPtr<agentc::ListreeValue> restoredRoot(restoredRootState.anchors[0].valueSid);
    ASSERT_TRUE(restoredRoot);
    auto restoredTitleItem = restoredRoot->find("title");
    ASSERT_TRUE(restoredTitleItem);
    auto restoredTitle = restoredTitleItem->getValue();
    ASSERT_TRUE(restoredTitle);
    EXPECT_EQ(std::string(static_cast<const char*>(restoredTitle->getData()), restoredTitle->getLength()), "anchored-root");

    auto restoredChildItem = restoredRoot->find("child");
    ASSERT_TRUE(restoredChildItem);
    auto restoredChild = restoredChildItem->getValue();
    ASSERT_TRUE(restoredChild);
    auto restoredLeafItem = restoredChild->find("leaf");
    ASSERT_TRUE(restoredLeafItem);
    auto restoredLeaf = restoredLeafItem->getValue();
    ASSERT_TRUE(restoredLeaf);
    EXPECT_EQ(std::string(static_cast<const char*>(restoredLeaf->getData()), restoredLeaf->getLength()), "anchored-leaf");

    std::vector<std::string> names;
    restoredRoot->forEachTree([&](const std::string& name, CPtr<agentc::ListreeItem>&) {
        names.push_back(name);
    });
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "child");
    EXPECT_EQ(names[1], "title");

    resetStructuredListreeAllocators();
}

TEST(CPtrTest, FileArenaStoreRestoresAnchoredTopLevelTreeState) {
    const std::string path = "/tmp/j3_root_anchor_store_test.txt";
    std::remove((path + ".value").c_str());
    std::remove((path + ".ref").c_str());
    std::remove((path + ".node").c_str());
    std::remove((path + ".item").c_str());
    std::remove((path + ".tree").c_str());
    std::remove((path + ".state").c_str());
    std::remove((path + ".state.root.bootstrap").c_str());
    FileArenaStore valueStore(path + ".value");
    FileArenaStore refStore(path + ".ref");
    FileArenaStore nodeStore(path + ".node");
    FileArenaStore itemStore(path + ".item");
    FileArenaStore treeStore(path + ".tree");
    FileArenaStore stateStore(path + ".state");
    roundTripAnchoredTreeStateThroughStores(valueStore, refStore, nodeStore, itemStore, treeStore, stateStore);
    std::remove((path + ".value").c_str());
    std::remove((path + ".ref").c_str());
    std::remove((path + ".node").c_str());
    std::remove((path + ".item").c_str());
    std::remove((path + ".tree").c_str());
    std::remove((path + ".state").c_str());
    std::remove((path + ".state.root.bootstrap").c_str());
    for (int i = 0; i < 4; ++i) {
        std::remove((path + ".value.slab." + std::to_string(i)).c_str());
        std::remove((path + ".ref.slab." + std::to_string(i)).c_str());
        std::remove((path + ".node.slab." + std::to_string(i)).c_str());
        std::remove((path + ".item.slab." + std::to_string(i)).c_str());
        std::remove((path + ".tree.slab." + std::to_string(i)).c_str());
    }
}

TEST(CPtrTest, LmdbArenaStoreRestoresAnchoredTopLevelTreeState) {
    const std::string path = "/tmp/j3_lmdb_root_anchor_store_test";
    std::filesystem::remove_all(path);
    LmdbArenaStore valueStore(path + "/value");
    LmdbArenaStore refStore(path + "/ref");
    LmdbArenaStore nodeStore(path + "/node");
    LmdbArenaStore itemStore(path + "/item");
    LmdbArenaStore treeStore(path + "/tree");
    LmdbArenaStore stateStore(path + "/state");
    ASSERT_TRUE(valueStore.isAvailable());
    roundTripAnchoredTreeStateThroughStores(valueStore, refStore, nodeStore, itemStore, treeStore, stateStore);
    std::filesystem::remove_all(path);
}

template <typename Store>
void roundTripMultiAnchorStateAndResumeMutation(Store& valueStore,
                                                Store& refStore,
                                                Store& nodeStore,
                                                Store& itemStore,
                                                Store& treeStore,
                                                Store& stateStore) {
    resetStructuredListreeAllocators();

    auto root = agentc::createNullValue();
    auto list = agentc::createListValue();
    auto child = agentc::createNullValue();
    agentc::addListItem(list, agentc::createStringValue("first"));
    agentc::addListItem(list, agentc::createStringValue("second"));
    agentc::addNamedItem(child, "kind", agentc::createStringValue("child"));
    agentc::addNamedItem(root, "list", list);
    agentc::addNamedItem(root, "child", child);

    ASSERT_TRUE(stateStore.saveNamedCheckpoint("bootstrap", Allocator<agentc::ListreeValue>::getAllocator().exportArenaMetadata()));

    std::vector<ArenaSlabImage> valueImages;
    std::vector<ArenaSlabImage> refImages;
    std::vector<ArenaSlabImage> nodeImages;
    std::vector<ArenaSlabImage> itemImages;
    std::vector<ArenaSlabImage> treeImages;
    saveAllocatorImagesToStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages);
    saveAllocatorImagesToStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages);
    saveAllocatorImagesToStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages);
    saveAllocatorImagesToStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages);
    saveAllocatorImagesToStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), treeImages);
    ASSERT_TRUE(stateStore.saveRootState("bootstrap", makeRootState({
        {"root", root.getSlabId()},
        {"list", list.getSlabId()},
        {"child", child.getSlabId()},
    })));

    resetStructuredListreeAllocators();

    ArenaCheckpointMetadata metadata;
    ASSERT_TRUE(stateStore.loadNamedCheckpoint("bootstrap", metadata));
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().restoreArenaMetadata(metadata));
    restoreAllocatorImagesFromStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages);
    restoreAllocatorImagesFromStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages);
    restoreAllocatorImagesFromStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages);
    restoreAllocatorImagesFromStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages);
    restoreAllocatorImagesFromStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), treeImages);

    ArenaRootState restored;
    ASSERT_TRUE(stateStore.loadRootState("bootstrap", restored));
    ASSERT_EQ(restored.anchors.size(), 3u);

    SlabId rootSid;
    SlabId listSid;
    SlabId childSid;
    for (const auto& anchor : restored.anchors) {
        if (anchor.name == "root") rootSid = anchor.valueSid;
        if (anchor.name == "list") listSid = anchor.valueSid;
        if (anchor.name == "child") childSid = anchor.valueSid;
    }

    CPtr<agentc::ListreeValue> restoredRoot(rootSid);
    CPtr<agentc::ListreeValue> restoredList(listSid);
    CPtr<agentc::ListreeValue> restoredChild(childSid);
    ASSERT_TRUE(restoredRoot);
    ASSERT_TRUE(restoredList);
    ASSERT_TRUE(restoredChild);

    agentc::addListItem(restoredList, agentc::createStringValue("third"));

    auto rootListItem = restoredRoot->find("list");
    ASSERT_TRUE(rootListItem);
    auto rootList = rootListItem->getValue();
    ASSERT_TRUE(rootList);
    std::vector<std::string> values;
    rootList->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue() && ref->getValue()->getData()) {
            values.emplace_back(static_cast<const char*>(ref->getValue()->getData()), ref->getValue()->getLength());
        }
    });
    std::reverse(values.begin(), values.end());
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], "first");
    EXPECT_EQ(values[1], "second");
    EXPECT_EQ(values[2], "third");

    auto childKind = restoredChild->find("kind");
    ASSERT_TRUE(childKind);
    EXPECT_EQ(std::string(static_cast<const char*>(childKind->getValue()->getData()), childKind->getValue()->getLength()), "child");

    resetStructuredListreeAllocators();
}

TEST(CPtrTest, FileArenaStoreRestoresMultipleAnchorsAndSupportsResumedMutation) {
    const std::string path = "/tmp/j3_multi_root_anchor_store_test.txt";
    std::remove((path + ".value").c_str());
    std::remove((path + ".ref").c_str());
    std::remove((path + ".node").c_str());
    std::remove((path + ".item").c_str());
    std::remove((path + ".tree").c_str());
    std::remove((path + ".state").c_str());
    std::remove((path + ".state.root.bootstrap").c_str());
    FileArenaStore valueStore(path + ".value");
    FileArenaStore refStore(path + ".ref");
    FileArenaStore nodeStore(path + ".node");
    FileArenaStore itemStore(path + ".item");
    FileArenaStore treeStore(path + ".tree");
    FileArenaStore stateStore(path + ".state");
    roundTripMultiAnchorStateAndResumeMutation(valueStore, refStore, nodeStore, itemStore, treeStore, stateStore);
    std::remove((path + ".value").c_str());
    std::remove((path + ".ref").c_str());
    std::remove((path + ".node").c_str());
    std::remove((path + ".item").c_str());
    std::remove((path + ".tree").c_str());
    std::remove((path + ".state").c_str());
    std::remove((path + ".state.root.bootstrap").c_str());
    for (int i = 0; i < 4; ++i) {
        std::remove((path + ".value.slab." + std::to_string(i)).c_str());
        std::remove((path + ".ref.slab." + std::to_string(i)).c_str());
        std::remove((path + ".node.slab." + std::to_string(i)).c_str());
        std::remove((path + ".item.slab." + std::to_string(i)).c_str());
        std::remove((path + ".tree.slab." + std::to_string(i)).c_str());
    }
}
