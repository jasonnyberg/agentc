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

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/alloc.h"
#include "core/cursor.h"
#include "listree/listree.h"

namespace {

void resetStructuredListreeAllocators() {
    Allocator<agentc::ListreeValue>::getAllocator().resetForTests();
    Allocator<agentc::ListreeValueRef>::getAllocator().resetForTests();
    Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().resetForTests();
    Allocator<agentc::ListreeItem>::getAllocator().resetForTests();
    Allocator<AATree<agentc::ListreeItem>>::getAllocator().resetForTests();
}

ArenaRootState makeRootState(std::initializer_list<ArenaRootAnchor> anchors) {
    ArenaRootState rootState;
    for (const auto& anchor : anchors) {
        rootState.anchors.push_back(anchor);
    }
    return rootState;
}

template <typename T>
bool saveAllocatorImagesToStore(ArenaStore& store, Allocator<T>& allocator, std::vector<ArenaSlabImage>& imagesOut) {
    imagesOut = allocator.exportSlabImages();
    if (imagesOut.empty()) {
        return false;
    }
    for (const auto& image : imagesOut) {
        if (!store.saveSlab(image)) {
            return false;
        }
    }
    return true;
}

template <typename T>
bool restoreAllocatorImagesFromStore(ArenaStore& store, Allocator<T>& allocator, const std::vector<ArenaSlabImage>& savedImages) {
    std::vector<ArenaSlabImage> restored;
    for (const auto& image : savedImages) {
        ArenaSlabImage loaded;
        if (!store.loadSlab(image.slabIndex, loaded)) {
            return false;
        }
        restored.push_back(std::move(loaded));
    }
    return allocator.restoreSlabImages(restored);
}

std::string readStringValue(const CPtr<agentc::ListreeValue>& value) {
    if (!value || !value->getData()) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

} // namespace

int main() {
    auto& allocator = Allocator<int>::getAllocator();
    allocator.resetForTests();

    auto checkpoint = allocator.checkpoint();
    if (!checkpoint.valid) {
        std::cerr << "failed to create checkpoint\n";
        return 1;
    }

    const std::string path = "/tmp/j3_arena_metadata_demo.txt";
    std::remove(path.c_str());
    FileArenaStore store(path);
    if (!store.saveNamedCheckpoint("bootstrap", allocator.exportArenaMetadata())) {
        std::cerr << "failed to save metadata checkpoint\n";
        return 1;
    }

    allocator.resetForTests();

    ArenaCheckpointMetadata restored;
    if (!store.loadNamedCheckpoint("bootstrap", restored) || !allocator.restoreArenaMetadata(restored)) {
        std::cerr << "failed to restore metadata checkpoint\n";
        return 1;
    }

    auto revived = allocator.currentCheckpoint();
    if (!revived.valid) {
        std::cerr << "restored allocator has no active checkpoint\n";
        return 1;
    }

    CPtr<int> transient(123);
    SlabId sid = transient.getSlabId();
    std::cout << "restored checkpoint depth: " << restored.checkpointLogStarts.size() << "\n";
    std::cout << "transient allocation after restart: " << sid << "\n";

    if (!allocator.rollback(revived)) {
        std::cerr << "failed to rollback restored checkpoint\n";
        return 1;
    }

    std::cout << "allocation valid after rollback: " << (allocator.valid(sid) ? "yes" : "no") << "\n";
    std::remove(path.c_str());

    allocator.resetForTests();
    auto imageCheckpoint = allocator.checkpoint();
    if (!imageCheckpoint.valid) {
        std::cerr << "failed to create slab-image checkpoint\n";
        return 1;
    }

    std::vector<CPtr<int>> values;
    values.reserve(SLAB_SIZE + 4);
    for (int i = 0; i < static_cast<int>(SLAB_SIZE) + 4; ++i) {
        values.emplace_back(i + 2000);
    }
    SlabId firstValueSid = values.front().getSlabId();
    SlabId lastValueSid = values.back().getSlabId();

    FileArenaStore slabStore(path);
    if (!slabStore.saveNamedCheckpoint("slabs", allocator.exportArenaMetadata())) {
        std::cerr << "failed to save slab metadata\n";
        return 1;
    }
    auto images = allocator.exportSlabImages();
    for (const auto& image : images) {
        if (!slabStore.saveSlab(image)) {
            std::cerr << "failed to save slab image\n";
            return 1;
        }
    }

    allocator.resetForTests();
    ArenaCheckpointMetadata slabMetadata;
    if (!slabStore.loadNamedCheckpoint("slabs", slabMetadata) || !allocator.restoreArenaMetadata(slabMetadata)) {
        std::cerr << "failed to restore slab metadata\n";
        return 1;
    }

    std::vector<ArenaSlabImage> restoredImages;
    for (const auto& image : images) {
        ArenaSlabImage restoredImage;
        if (!slabStore.loadSlab(image.slabIndex, restoredImage)) {
            std::cerr << "failed to load slab image\n";
            return 1;
        }
        restoredImages.push_back(std::move(restoredImage));
    }
    if (!allocator.restoreSlabImages(restoredImages)) {
        std::cerr << "failed to restore slab images\n";
        return 1;
    }

    std::cout << "restored first slab-backed value: " << *CPtr<int>(firstValueSid) << "\n";
    std::cout << "restored last slab-backed value: " << *CPtr<int>(lastValueSid) << "\n";

    const std::string structuredBase = "/tmp/j3_structured_tree_demo";
    std::remove((structuredBase + ".value").c_str());
    std::remove((structuredBase + ".ref").c_str());
    std::remove((structuredBase + ".node").c_str());
    std::remove((structuredBase + ".item").c_str());
    std::remove((structuredBase + ".tree").c_str());
    std::remove((structuredBase + ".state").c_str());
    std::remove((structuredBase + ".state.root.bootstrap").c_str());

    resetStructuredListreeAllocators();

    auto root = agentc::createNullValue();
    auto title = agentc::createStringValue("root-title");
    auto child = agentc::createNullValue();
    auto childLeaf = agentc::createStringValue("nested-value");
    auto list = agentc::createListValue();
    agentc::addNamedItem(child, "leaf", childLeaf);
    agentc::addListItem(list, agentc::createStringValue("first"));
    agentc::addListItem(list, agentc::createStringValue("second"));
    agentc::addNamedItem(root, "title", title);
    agentc::addNamedItem(root, "child", child);
    agentc::addNamedItem(root, "list", list);

    const SlabId rootSid = root.getSlabId();

    FileArenaStore valueStore(structuredBase + ".value");
    FileArenaStore refStore(structuredBase + ".ref");
    FileArenaStore nodeStore(structuredBase + ".node");
    FileArenaStore itemStore(structuredBase + ".item");
    FileArenaStore treeStore(structuredBase + ".tree");
    FileArenaStore stateStore(structuredBase + ".state");

    std::vector<ArenaSlabImage> valueImages;
    std::vector<ArenaSlabImage> refImages;
    std::vector<ArenaSlabImage> nodeImages;
    std::vector<ArenaSlabImage> itemImages;
    std::vector<ArenaSlabImage> treeImages;
    auto rootState = makeRootState({
        {"root", rootSid},
        {"list", list.getSlabId()},
        {"child", child.getSlabId()},
    });
    if (!stateStore.saveNamedCheckpoint("bootstrap", Allocator<agentc::ListreeValue>::getAllocator().exportArenaMetadata()) ||
        !stateStore.saveRootState("bootstrap", rootState)) {
        std::cerr << "failed to save structured Listree root state\n";
        return 1;
    }
    if (!saveAllocatorImagesToStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages) ||
        !saveAllocatorImagesToStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages) ||
        !saveAllocatorImagesToStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages) ||
        !saveAllocatorImagesToStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages) ||
        !saveAllocatorImagesToStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), treeImages)) {
        std::cerr << "failed to save structured Listree slab images\n";
        return 1;
    }

    resetStructuredListreeAllocators();

    ArenaCheckpointMetadata rootMetadata;
    if (!stateStore.loadNamedCheckpoint("bootstrap", rootMetadata) ||
        !Allocator<agentc::ListreeValue>::getAllocator().restoreArenaMetadata(rootMetadata)) {
        std::cerr << "failed to restore structured Listree root metadata\n";
        return 1;
    }

    if (!restoreAllocatorImagesFromStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), valueImages) ||
        !restoreAllocatorImagesFromStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), refImages) ||
        !restoreAllocatorImagesFromStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), nodeImages) ||
        !restoreAllocatorImagesFromStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), itemImages) ||
        !restoreAllocatorImagesFromStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), treeImages)) {
        std::cerr << "failed to restore structured Listree slab images\n";
        return 1;
    }

    ArenaRootState restoredRootState;
    if (!stateStore.loadRootState("bootstrap", restoredRootState) || restoredRootState.anchors.empty()) {
        std::cerr << "failed to restore structured Listree root state\n";
        return 1;
    }

    SlabId restoredRootSid;
    SlabId restoredListSid;
    SlabId restoredChildSid;
    std::cout << "restored anchors:";
    for (const auto& anchor : restoredRootState.anchors) {
        std::cout << ' ' << anchor.name << '=' << anchor.valueSid;
        if (anchor.name == "root") restoredRootSid = anchor.valueSid;
        if (anchor.name == "list") restoredListSid = anchor.valueSid;
        if (anchor.name == "child") restoredChildSid = anchor.valueSid;
    }
    std::cout << "\n";

    CPtr<agentc::ListreeValue> restoredRoot(restoredRootSid);
    std::cout << "restored tree keys:";
    restoredRoot->forEachTree([&](const std::string& name, CPtr<agentc::ListreeItem>&) {
        std::cout << ' ' << name;
    });
    std::cout << "\n";

    auto restoredTitle = restoredRoot->find("title");
    auto restoredChild = restoredRoot->find("child");
    auto restoredList = restoredRoot->find("list");
    if (!restoredTitle || !restoredChild || !restoredList) {
        std::cerr << "restored top-level tree is missing expected items\n";
        return 1;
    }

    std::cout << "restored title: " << readStringValue(restoredTitle->getValue()) << "\n";
    std::cout << "restored child leaf: " << readStringValue(restoredChild->getValue()->find("leaf")->getValue()) << "\n";
    auto restoredListValue = restoredList->getValue();
    std::cout << "restored list endpoints: "
              << readStringValue(restoredListValue->get())
              << ", "
              << readStringValue(restoredListValue->get(false, true))
              << "\n";

    CPtr<agentc::ListreeValue> anchoredList(restoredListSid);
    CPtr<agentc::ListreeValue> anchoredChild(restoredChildSid);
    if (!anchoredList || !anchoredChild) {
        std::cerr << "failed to recover secondary anchors\n";
        return 1;
    }
    agentc::addListItem(anchoredList, agentc::createStringValue("third"));
    std::cout << "resumed anchored list tail: " << readStringValue(anchoredList->get(false, true)) << "\n";
    std::cout << "resumed anchored child kind: " << readStringValue(anchoredChild->find("leaf")->getValue()) << "\n";

    resetStructuredListreeAllocators();
    auto iteratorValue = agentc::createCursorValue(new agentc::Cursor());
    auto rejectedImages = Allocator<agentc::ListreeValue>::getAllocator().exportSlabImages();
    std::cout << "iterator payload persistence rejected: " << (rejectedImages.empty() ? "yes" : "no") << "\n";

    resetStructuredListreeAllocators();
    std::remove((structuredBase + ".value").c_str());
    std::remove((structuredBase + ".ref").c_str());
    std::remove((structuredBase + ".node").c_str());
    std::remove((structuredBase + ".item").c_str());
    std::remove((structuredBase + ".tree").c_str());
    std::remove((structuredBase + ".state").c_str());
    std::remove((structuredBase + ".state.root.bootstrap").c_str());
    for (int i = 0; i < 4; ++i) {
        std::remove((structuredBase + ".value.slab." + std::to_string(i)).c_str());
        std::remove((structuredBase + ".ref.slab." + std::to_string(i)).c_str());
        std::remove((structuredBase + ".node.slab." + std::to_string(i)).c_str());
        std::remove((structuredBase + ".item.slab." + std::to_string(i)).c_str());
        std::remove((structuredBase + ".tree.slab." + std::to_string(i)).c_str());
    }

    return allocator.valid(sid) ? 1 : 0;
}
