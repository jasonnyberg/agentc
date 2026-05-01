#include "session_state_store.h"

#include "../../../core/alloc.h"
#include "../../../listree/listree.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace agentc::runtime {
namespace {

template <typename T>
bool saveAllocatorImagesToStore(ArenaStore& store, Allocator<T>& allocator, std::string* error) {
    const auto images = allocator.exportSlabImages();
    if (images.empty()) {
        if (error) *error = "allocator exported no slab images";
        return false;
    }
    for (const auto& image : images) {
        if (!store.saveSlab(image)) {
            if (error) *error = "failed to save slab image";
            return false;
        }
    }
    return true;
}

template <typename T>
bool loadAllocatorImagesFromStore(ArenaStore& store, Allocator<T>& allocator, std::string* error) {
    std::vector<ArenaSlabImage> restored;
    for (int i = 0; i < NUM_SLABS; ++i) {
        ArenaSlabImage image;
        if (store.loadSlab(static_cast<uint16_t>(i), image)) {
            restored.push_back(std::move(image));
        }
    }
    if (restored.empty()) {
        if (error) *error = "no slab images found in store";
        return false;
    }
    if (!allocator.restoreSlabImages(restored)) {
        if (error) *error = "failed to restore slab images";
        return false;
    }
    return true;
}

void resetStructuredListreeAllocators() {
    Allocator<agentc::ListreeValue>::getAllocator().resetForTests();
    Allocator<agentc::ListreeValueRef>::getAllocator().resetForTests();
    Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().resetForTests();
    Allocator<agentc::ListreeItem>::getAllocator().resetForTests();
    Allocator<AATree<agentc::ListreeItem>>::getAllocator().resetForTests();
}

ArenaRootState makeRootState(const std::string& name, SlabId sid) {
    ArenaRootState rootState;
    rootState.anchors.push_back(ArenaRootAnchor{name, sid});
    return rootState;
}

SlabId findAnchor(const ArenaRootState& rootState, const std::string& name) {
    for (const auto& anchor : rootState.anchors) {
        if (anchor.name == name) {
            return anchor.valueSid;
        }
    }
    return {};
}

} // namespace

SessionStateStore::SessionStateStore(std::string base_path)
    : base_path_(std::move(base_path)) {}

bool SessionStateStore::exists() const {
    return std::filesystem::exists(statePath() + ".root.bootstrap");
}

bool SessionStateStore::load(nlohmann::json& out, std::string* error) const {
    if (!exists()) {
        if (error) *error = "session state does not exist";
        return false;
    }

    resetStructuredListreeAllocators();

    FileArenaStore valueStore(valuePath());
    FileArenaStore refStore(refPath());
    FileArenaStore nodeStore(nodePath());
    FileArenaStore itemStore(itemPath());
    FileArenaStore treeStore(treePath());
    FileArenaStore stateStore(statePath());

    ArenaCheckpointMetadata rootMetadata;
    if (!stateStore.loadNamedCheckpoint("bootstrap", rootMetadata) ||
        !Allocator<agentc::ListreeValue>::getAllocator().restoreArenaMetadata(rootMetadata)) {
        if (error) *error = "failed to restore root arena metadata";
        return false;
    }

    if (!loadAllocatorImagesFromStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), error) ||
        !loadAllocatorImagesFromStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), error) ||
        !loadAllocatorImagesFromStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), error) ||
        !loadAllocatorImagesFromStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), error) ||
        !loadAllocatorImagesFromStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), error)) {
        return false;
    }

    ArenaRootState rootState;
    if (!stateStore.loadRootState("bootstrap", rootState)) {
        if (error) *error = "failed to load root state";
        return false;
    }

    const SlabId rootSid = findAnchor(rootState, "session");
    if (rootSid == SlabId{}) {
        if (error) *error = "missing session root anchor";
        return false;
    }

    CPtr<agentc::ListreeValue> root(rootSid);
    if (!root) {
        if (error) *error = "restored session root is null";
        return false;
    }

    try {
        out = nlohmann::json::parse(agentc::toJson(root));
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("failed to parse restored session json: ") + e.what();
        return false;
    }
}

bool SessionStateStore::save(const nlohmann::json& state, std::string* error) const {
    removeStaleFiles();
    resetStructuredListreeAllocators();

    auto root = agentc::fromJson(state.dump());
    if (!root) {
        if (error) *error = "failed to materialize json into listree";
        return false;
    }

    FileArenaStore valueStore(valuePath());
    FileArenaStore refStore(refPath());
    FileArenaStore nodeStore(nodePath());
    FileArenaStore itemStore(itemPath());
    FileArenaStore treeStore(treePath());
    FileArenaStore stateStore(statePath());

    if (!stateStore.saveNamedCheckpoint("bootstrap", Allocator<agentc::ListreeValue>::getAllocator().exportArenaMetadata()) ||
        !stateStore.saveRootState("bootstrap", makeRootState("session", root.getSlabId()))) {
        if (error) *error = "failed to save root state";
        return false;
    }

    if (!saveAllocatorImagesToStore(valueStore, Allocator<agentc::ListreeValue>::getAllocator(), error) ||
        !saveAllocatorImagesToStore(refStore, Allocator<agentc::ListreeValueRef>::getAllocator(), error) ||
        !saveAllocatorImagesToStore(nodeStore, Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), error) ||
        !saveAllocatorImagesToStore(itemStore, Allocator<agentc::ListreeItem>::getAllocator(), error) ||
        !saveAllocatorImagesToStore(treeStore, Allocator<AATree<agentc::ListreeItem>>::getAllocator(), error)) {
        return false;
    }

    return true;
}

void SessionStateStore::clear() const {
    removeStaleFiles();
}

std::string SessionStateStore::valuePath() const { return base_path_ + ".value"; }
std::string SessionStateStore::refPath() const { return base_path_ + ".ref"; }
std::string SessionStateStore::nodePath() const { return base_path_ + ".node"; }
std::string SessionStateStore::itemPath() const { return base_path_ + ".item"; }
std::string SessionStateStore::treePath() const { return base_path_ + ".tree"; }
std::string SessionStateStore::statePath() const { return base_path_ + ".state"; }

void SessionStateStore::removeStaleFiles() const {
    std::remove(valuePath().c_str());
    std::remove(refPath().c_str());
    std::remove(nodePath().c_str());
    std::remove(itemPath().c_str());
    std::remove(treePath().c_str());
    std::remove(statePath().c_str());
    std::remove((statePath() + ".root.bootstrap").c_str());
    for (int i = 0; i < NUM_SLABS; ++i) {
        std::remove((valuePath() + ".slab." + std::to_string(i)).c_str());
        std::remove((refPath() + ".slab." + std::to_string(i)).c_str());
        std::remove((nodePath() + ".slab." + std::to_string(i)).c_str());
        std::remove((itemPath() + ".slab." + std::to_string(i)).c_str());
        std::remove((treePath() + ".slab." + std::to_string(i)).c_str());
    }
}

} // namespace agentc::runtime
