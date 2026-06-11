#include "session_state_store.h"

#include "../../../core/alloc.h"
#include "../../../listree/listree.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace agentc::runtime {
namespace {

// Allocator directory names inside the session image.
constexpr const char* kAllocatorNameValue = "value";
constexpr const char* kAllocatorNameRef   = "ref";
constexpr const char* kAllocatorNameNode  = "node";
constexpr const char* kAllocatorNameItem  = "item";
constexpr const char* kAllocatorNameTree  = "tree";
constexpr const char* kAllocatorNameBlob  = "blob";

std::string slabDirPath(const std::string& session_dir, const char* allocName) {
    return (std::filesystem::path(session_dir) / "allocators" / allocName).string();
}

void resetStructuredListreeAllocators() {
    Allocator<agentc::ListreeValue>::getAllocator().resetForTests();
    Allocator<agentc::ListreeValueRef>::getAllocator().resetForTests();
    Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().resetForTests();
    Allocator<agentc::ListreeItem>::getAllocator().resetForTests();
    Allocator<AATree<agentc::ListreeItem>>::getAllocator().resetForTests();
    BlobAllocator::getAllocator().clearAllSlabs();
}

ArenaRootState makeRootState(const std::string& name, SlabId sid) {
    ArenaRootState root_state;
    root_state.anchors.push_back(ArenaRootAnchor{name, sid});
    return root_state;
}

std::string sanitize_session_name(std::string value) {
    if (value.empty()) {
        return "default";
    }
    std::replace_if(value.begin(), value.end(), [](unsigned char ch) {
        return !(std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.');
    }, '_');
    return value.empty() ? "default" : value;
}

SlabId findAnchor(const ArenaRootState& root_state, const std::string& name) {
    for (const auto& anchor : root_state.anchors) {
        if (anchor.name == name) {
            return anchor.valueSid;
        }
    }
    return {};
}

std::string slabEncodingName(ArenaSlabEncoding encoding) {
    switch (encoding) {
        case ArenaSlabEncoding::RawBytes:
            return "raw_bytes";
        case ArenaSlabEncoding::Structured:
            return "structured";
    }
    return "unknown";
}

template <typename T>
std::string allocatorEncodingName() {
    return slabEncodingName(ArenaPersistenceTraits<T>::encoding);
}

template <typename T>
SessionImageAllocatorManifest makeAllocatorManifest(const std::string& logical_name,
                                                   const std::string& type_name,
                                                   const std::vector<ArenaSlabImage>& images) {
    SessionImageAllocatorManifest manifest;
    manifest.name = logical_name;
    manifest.type = type_name;
    manifest.encoding = images.empty() ? allocatorEncodingName<T>() : slabEncodingName(images.front().encoding);
    manifest.item_size_bytes = sizeof(T);
    manifest.metadata_file = "allocators/" + logical_name + "/meta.bin";
    for (const auto& image : images) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "slab.%04u.bin", image.slabIndex);
        SessionImageSlabFile slab_file;
        slab_file.index = image.slabIndex;
        slab_file.file = "allocators/" + logical_name + "/" + buffer;
        manifest.slabs.push_back(std::move(slab_file));
    }
    return manifest;
}

ArenaCheckpointMetadata synthesizeMetadataFromImages(const std::vector<ArenaSlabImage>& images) {
    ArenaCheckpointMetadata metadata;
    SlabId highest;
    for (const auto& image : images) {
        bool slab_has_live_slots = false;
        for (size_t offset = 0; offset < image.inUse.size(); ++offset) {
            if (image.inUse[offset] == 0) {
                continue;
            }
            ++metadata.liveSlotCount;
            slab_has_live_slots = true;
            highest = SlabId(image.slabIndex, static_cast<uint16_t>(offset));
        }
        if (slab_has_live_slots) {
            ++metadata.activeSlabCount;
        }
    }
    metadata.highestSlabIndex = highest.first;
    metadata.highestSlabOffset = highest.second;
    return metadata;
}

template <typename T>
bool exportAllocatorManifestAndImagesFromCheckpoint(const std::string& logical_name,
                                                    const std::string& type_name,
                                                    const typename Allocator<T>::Checkpoint& checkpoint,
                                                    SessionImageAllocatorManifest& allocator_manifest,
                                                    ArenaCheckpointMetadata& metadata,
                                                    std::vector<ArenaSlabImage>& images,
                                                    std::string* error) {
    auto& allocator = Allocator<T>::getAllocator();
    images = allocator.exportSlabImagesSince(checkpoint);
    metadata = synthesizeMetadataFromImages(images);
    if constexpr (std::is_same_v<T, agentc::ListreeValue>) {
        if (images.empty()) {
            if (error) *error = "native root snapshot exported no value slab images";
            return false;
        }
    }
    allocator_manifest = makeAllocatorManifest<T>(logical_name, type_name, images);
    return true;
}

SessionImageBootstrapAllocator makeBootstrapAllocator(const SessionImageAllocatorManifest& manifest) {
    SessionImageBootstrapAllocator bootstrap;
    bootstrap.name = manifest.name;
    bootstrap.type = manifest.type;
    bootstrap.encoding = manifest.encoding;
    bootstrap.item_size_bytes = manifest.item_size_bytes;
    bootstrap.metadata_file = manifest.metadata_file;
    return bootstrap;
}

SessionImageAllocatorManifest makeAllocatorDescriptor(const SessionImageBootstrapAllocator& bootstrap) {
    SessionImageAllocatorManifest manifest;
    manifest.name = bootstrap.name;
    manifest.type = bootstrap.type;
    manifest.encoding = bootstrap.encoding;
    manifest.item_size_bytes = bootstrap.item_size_bytes;
    manifest.metadata_file = bootstrap.metadata_file;
    return manifest;
}

SessionImageBootstrap makeBootstrapFromManifest(const SessionImageManifest& manifest) {
    SessionImageBootstrap bootstrap;
    bootstrap.version = manifest.version;
    bootstrap.session = manifest.session;
    bootstrap.roots_file = manifest.roots_file;
    bootstrap.allocators.reserve(manifest.allocators.size());
    for (const auto& allocator : manifest.allocators) {
        bootstrap.allocators.push_back(makeBootstrapAllocator(allocator));
    }
    return bootstrap;
}

template <typename T>
bool restoreAllocatorFromDescriptor(const SessionImageStore& image_store,
                                  const SessionImageBootstrapAllocator& allocator_bootstrap,
                                  std::string* error) {
    const auto allocator_manifest = makeAllocatorDescriptor(allocator_bootstrap);
    ArenaCheckpointMetadata metadata;
    if (!image_store.loadAllocatorMetadata(allocator_manifest, metadata, error)) {
        return false;
    }

    auto& allocator = Allocator<T>::getAllocator();
    if (!allocator.restoreArenaMetadata(metadata)) {
        if (error) *error = "failed to restore allocator metadata";
        return false;
    }

    std::vector<ArenaSlabImage> images;
    if (!image_store.loadAllocatorSlabs(allocator_manifest, images, error)) {
        return false;
    }
    if (images.empty()) {
        return metadata.liveSlotCount == 0;
    }
    if (!allocator.restoreSlabImages(images)) {
        if (error) *error = "failed to restore allocator slab images";
        return false;
    }
    return true;
}

const SessionImageBootstrapAllocator* findAllocatorBootstrap(const SessionImageBootstrap& bootstrap,
                                                                 const std::string& name) {
    for (const auto& allocator : bootstrap.allocators) {
        if (allocator.name == name) {
            return &allocator;
        }
    }
    return nullptr;
}

} // namespace

SessionStateStore::SessionStateStore(std::string root_path, std::string session_name)
    : root_path_(std::move(root_path)),
      session_name_(sanitize_session_name(std::move(session_name))) {}

void SessionStateStore::configureFileBackedAllocators() {
    file_backed_ = true;
    const std::string session_dir =
        (std::filesystem::path(root_path_) / session_name_).string();

    auto configure = [&](auto& allocator, const char* name, const char* prefix) {
        allocator.configureMmapFileBackedSlabs(slabDirPath(session_dir, name), prefix);
    };

    configure(Allocator<agentc::ListreeValue>::getAllocator(),        kAllocatorNameValue, "slab");
    configure(Allocator<agentc::ListreeValueRef>::getAllocator(),     kAllocatorNameRef,   "slab");
    configure(Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), kAllocatorNameNode, "slab");
    configure(Allocator<agentc::ListreeItem>::getAllocator(),         kAllocatorNameItem,  "slab");
    configure(Allocator<AATree<agentc::ListreeItem>>::getAllocator(),  kAllocatorNameTree, "slab");
    BlobAllocator::getAllocator().configureMmapFileBackedSlabs(
        slabDirPath(session_dir, kAllocatorNameBlob), "blob");
}


bool SessionStateStore::exists() const {
    return sessionImageStore().exists();
}

bool SessionStateStore::loadRoot(CPtr<agentc::ListreeValue>& out, std::string* error) const {
    if (file_backed_) return loadRootFileBacked(out, error);
    auto image_store = sessionImageStore();
    if (!image_store.exists()) {
        if (error) *error = "session state does not exist";
        return false;
    }

    resetStructuredListreeAllocators();

    SessionImageBootstrap bootstrap;
    if (std::filesystem::exists(image_store.bootstrapPath())) {
        if (!image_store.loadBootstrap(bootstrap, error)) {
            return false;
        }
    } else {
        SessionImageManifest manifest;
        if (!image_store.loadManifest(manifest, error)) {
            return false;
        }
        bootstrap = makeBootstrapFromManifest(manifest);
    }

    const auto* value_bootstrap = findAllocatorBootstrap(bootstrap, "value");
    const auto* ref_bootstrap = findAllocatorBootstrap(bootstrap, "ref");
    const auto* node_bootstrap = findAllocatorBootstrap(bootstrap, "node");
    const auto* item_bootstrap = findAllocatorBootstrap(bootstrap, "item");
    const auto* tree_bootstrap = findAllocatorBootstrap(bootstrap, "tree");
    if (!value_bootstrap || !ref_bootstrap || !node_bootstrap || !item_bootstrap || !tree_bootstrap) {
        if (error) *error = "session bootstrap is missing required allocators";
        return false;
    }

    if (!restoreAllocatorFromDescriptor<agentc::ListreeValue>(image_store, *value_bootstrap, error) ||
        !restoreAllocatorFromDescriptor<agentc::ListreeValueRef>(image_store, *ref_bootstrap, error) ||
        !restoreAllocatorFromDescriptor<CLL<agentc::ListreeValueRef>>(image_store, *node_bootstrap, error) ||
        !restoreAllocatorFromDescriptor<agentc::ListreeItem>(image_store, *item_bootstrap, error) ||
        !restoreAllocatorFromDescriptor<AATree<agentc::ListreeItem>>(image_store, *tree_bootstrap, error)) {
        return false;
    }

    ArenaRootState root_state;
    if (!image_store.loadRootState(bootstrap.roots_file, root_state, error)) {
        return false;
    }

    const SlabId root_sid = findAnchor(root_state, "session");
    if (root_sid == SlabId{}) {
        if (error) *error = "missing session root anchor";
        return false;
    }

    out = CPtr<agentc::ListreeValue>(root_sid);
    if (!out) {
        if (error) *error = "restored session root is null";
        return false;
    }
    return true;
}

bool SessionStateStore::saveRoot(CPtr<agentc::ListreeValue> root, std::string* error) const {
    if (file_backed_) return saveRootFileBacked(root, error);
    if (!root) {
        if (error) *error = "cannot save null session root";
        return false;
    }

    auto& value_allocator = Allocator<agentc::ListreeValue>::getAllocator();
    auto& ref_allocator = Allocator<agentc::ListreeValueRef>::getAllocator();
    auto& node_allocator = Allocator<CLL<agentc::ListreeValueRef>>::getAllocator();
    auto& item_allocator = Allocator<agentc::ListreeItem>::getAllocator();
    auto& tree_allocator = Allocator<AATree<agentc::ListreeItem>>::getAllocator();
    auto& blob_allocator = BlobAllocator::getAllocator();

    const auto value_checkpoint = value_allocator.checkpoint();
    const auto ref_checkpoint = ref_allocator.checkpoint();
    const auto node_checkpoint = node_allocator.checkpoint();
    const auto item_checkpoint = item_allocator.checkpoint();
    const auto tree_checkpoint = tree_allocator.checkpoint();
    const auto blob_checkpoint = blob_allocator.checkpoint();

    const auto rollback_snapshot = [&]() {
        blob_allocator.rollback(blob_checkpoint);
        tree_allocator.rollback(tree_checkpoint);
        item_allocator.rollback(item_checkpoint);
        node_allocator.rollback(node_checkpoint);
        ref_allocator.rollback(ref_checkpoint);
        value_allocator.rollback(value_checkpoint);
    };

    CPtr<agentc::ListreeValue> snapshot_root;
    SessionImageAllocatorManifest value_manifest;
    SessionImageAllocatorManifest ref_manifest;
    SessionImageAllocatorManifest node_manifest;
    SessionImageAllocatorManifest item_manifest;
    SessionImageAllocatorManifest tree_manifest;
    ArenaCheckpointMetadata value_metadata;
    ArenaCheckpointMetadata ref_metadata;
    ArenaCheckpointMetadata node_metadata;
    ArenaCheckpointMetadata item_metadata;
    ArenaCheckpointMetadata tree_metadata;
    std::vector<ArenaSlabImage> value_images;
    std::vector<ArenaSlabImage> ref_images;
    std::vector<ArenaSlabImage> node_images;
    std::vector<ArenaSlabImage> item_images;
    std::vector<ArenaSlabImage> tree_images;

    try {
        // Force a full fresh-allocation copy via JSON round-trip so that all
        // nodes are newly slab-allocated AFTER the checkpoint.  root->copy()
        // short-circuits on read-only nodes (returning existing slabs) which
        // would make exportSlabImagesSince return an empty set.
        {
            const std::string json_str = agentc::toJson(root);
            if (json_str.empty()) {
                rollback_snapshot();
                if (error) *error = "failed to serialize agent root to JSON for snapshot";
                return false;
            }
            snapshot_root = agentc::fromJson(json_str);
        }
        if (!snapshot_root) {
            rollback_snapshot();
            if (error) *error = "failed to create native snapshot root";
            return false;
        }

        if (!exportAllocatorManifestAndImagesFromCheckpoint<agentc::ListreeValue>("value", "agentc::ListreeValue",
                                                                                   value_checkpoint,
                                                                                   value_manifest, value_metadata, value_images, error) ||
            !exportAllocatorManifestAndImagesFromCheckpoint<agentc::ListreeValueRef>("ref", "agentc::ListreeValueRef",
                                                                                      ref_checkpoint,
                                                                                      ref_manifest, ref_metadata, ref_images, error) ||
            !exportAllocatorManifestAndImagesFromCheckpoint<CLL<agentc::ListreeValueRef>>("node", "agentc::CLL<agentc::ListreeValueRef>",
                                                                                           node_checkpoint,
                                                                                           node_manifest, node_metadata, node_images, error) ||
            !exportAllocatorManifestAndImagesFromCheckpoint<agentc::ListreeItem>("item", "agentc::ListreeItem",
                                                                                  item_checkpoint,
                                                                                  item_manifest, item_metadata, item_images, error) ||
            !exportAllocatorManifestAndImagesFromCheckpoint<AATree<agentc::ListreeItem>>("tree", "agentc::AATree<agentc::ListreeItem>",
                                                                                          tree_checkpoint,
                                                                                          tree_manifest, tree_metadata, tree_images, error)) {
            rollback_snapshot();
            return false;
        }
    } catch (const std::exception& e) {
        rollback_snapshot();
        if (error) *error = std::string("failed to create native snapshot root: ") + e.what();
        return false;
    }

    const SlabId snapshot_root_sid = snapshot_root.getSlabId();
    snapshot_root = nullptr;
    rollback_snapshot();

    auto image_store = sessionImageStore();
    image_store.clear();

    SessionImageBootstrap bootstrap;
    bootstrap.session = image_store.sessionName();
    bootstrap.roots_file = "roots.bin";

    SessionImageManifest manifest;
    manifest.session = image_store.sessionName();
    manifest.roots_file = bootstrap.roots_file;

    if (!image_store.saveAllocatorMetadata(value_manifest, value_metadata, error) ||
        !image_store.saveAllocatorMetadata(ref_manifest, ref_metadata, error) ||
        !image_store.saveAllocatorMetadata(node_manifest, node_metadata, error) ||
        !image_store.saveAllocatorMetadata(item_manifest, item_metadata, error) ||
        !image_store.saveAllocatorMetadata(tree_manifest, tree_metadata, error) ||
        !image_store.saveAllocatorSlabs(value_manifest, value_images, error) ||
        !image_store.saveAllocatorSlabs(ref_manifest, ref_images, error) ||
        !image_store.saveAllocatorSlabs(node_manifest, node_images, error) ||
        !image_store.saveAllocatorSlabs(item_manifest, item_images, error) ||
        !image_store.saveAllocatorSlabs(tree_manifest, tree_images, error)) {
        return false;
    }

    manifest.allocators = {
        value_manifest,
        ref_manifest,
        node_manifest,
        item_manifest,
        tree_manifest,
    };
    bootstrap.allocators = {
        makeBootstrapAllocator(value_manifest),
        makeBootstrapAllocator(ref_manifest),
        makeBootstrapAllocator(node_manifest),
        makeBootstrapAllocator(item_manifest),
        makeBootstrapAllocator(tree_manifest),
    };

    if (!image_store.saveRootState(bootstrap.roots_file, makeRootState("session", snapshot_root_sid), error) ||
        !image_store.saveBootstrap(bootstrap, error) ||
        !image_store.saveManifest(manifest, error)) {
        return false;
    }

    return true;
}

bool SessionStateStore::saveRootFileBacked(CPtr<agentc::ListreeValue> root,
                                            std::string* error) const {
    if (!root) {
        if (error) *error = "cannot save null session root";
        return false;
    }

    // 1. Flush all live mmap'd slab files to disk.
    //    inUse arrays are already in the files (written on every alloc/free),
    //    so a single msync makes the entire allocator state durable.
    Allocator<agentc::ListreeValue>::getAllocator().flushMappedSlabs();
    Allocator<agentc::ListreeValueRef>::getAllocator().flushMappedSlabs();
    Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().flushMappedSlabs();
    Allocator<agentc::ListreeItem>::getAllocator().flushMappedSlabs();
    Allocator<AATree<agentc::ListreeItem>>::getAllocator().flushMappedSlabs();
    BlobAllocator::getAllocator().flushMappedSlabs();

    // 2. Persist slab[0] for each allocator (heap-backed sentinel slab).
    //    This ensures the root anchor survives process restart even when
    //    the root lives in slab[0].
    const std::string session_dir = sessionImageStore().sessionPath();
    auto writeSlabZero = [&](auto& allocator, const char* name, const char* prefix) {
        // Use the same naming convention as createOwnedSlab so reattach finds it.
        // Format: {prefix}.slab.{index}.bin (e.g., slab.slab.0000.bin)
        std::ostringstream fname;
        fname << prefix << ".slab.0000.bin";
        std::string slab0Path = (std::filesystem::path(session_dir) / "allocators" / name / fname.str()).string();
        std::string write_err;
        allocator.writeSlabToFile(0, slab0Path, &write_err);
    };
    writeSlabZero(Allocator<agentc::ListreeValue>::getAllocator(),        kAllocatorNameValue, "slab");
    writeSlabZero(Allocator<agentc::ListreeValueRef>::getAllocator(),     kAllocatorNameRef, "slab");
    writeSlabZero(Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), kAllocatorNameNode, "slab");
    writeSlabZero(Allocator<agentc::ListreeItem>::getAllocator(),         kAllocatorNameItem, "slab");
    writeSlabZero(Allocator<AATree<agentc::ListreeItem>>::getAllocator(),  kAllocatorNameTree, "slab");
    writeSlabZero(BlobAllocator::getAllocator(),                          kAllocatorNameBlob, "blob");

    // 3. Write root anchor and bootstrap — the slab files are already the
    //    authoritative persisted state.
    auto image_store = sessionImageStore();
    const SlabId root_sid = root.getSlabId();

    SessionImageBootstrap bootstrap;
    bootstrap.session    = image_store.sessionName();
    bootstrap.roots_file = "roots.bin";
    for (const char* name : {kAllocatorNameValue, kAllocatorNameRef,
                              kAllocatorNameNode,  kAllocatorNameItem,
                              kAllocatorNameTree,  kAllocatorNameBlob}) {
        SessionImageBootstrapAllocator ba;
        ba.name          = name;
        ba.encoding      = "file_backed_native";
        ba.metadata_file = std::string("allocators/") + name + "/meta.bin"; // placeholder
        // Include the slab files for this allocator in the bootstrap.
        // Use the same naming convention as createOwnedSlab.
        const char* prefix = (name == kAllocatorNameBlob) ? "blob" : "slab";
        std::ostringstream fname;
        fname << prefix << ".slab.0000.bin";
        ba.files = {std::string("allocators/") + name + "/" + fname.str()};
        // Set type and item_size_bytes so the bootstrap round-trips through
        // bootstrapFromJson, which validates !allocator.type.empty() and
        // requires item_size_bytes to be present and numeric.
        ba.item_size_bytes = sizeof(agentc::ListreeValue);
        if (name == kAllocatorNameValue) {
            ba.type = "agentc::ListreeValue";
        } else if (name == kAllocatorNameRef) {
            ba.type = "agentc::ListreeValueRef";
            ba.item_size_bytes = sizeof(agentc::ListreeValueRef);
        } else if (name == kAllocatorNameNode) {
            ba.type = "CLL<agentc::ListreeValueRef>";
            ba.item_size_bytes = sizeof(CLL<agentc::ListreeValueRef>);
        } else if (name == kAllocatorNameItem) {
            ba.type = "agentc::ListreeItem";
            ba.item_size_bytes = sizeof(agentc::ListreeItem);
        } else if (name == kAllocatorNameTree) {
            ba.type = "AATree<agentc::ListreeItem>";
            ba.item_size_bytes = sizeof(AATree<agentc::ListreeItem>);
        } else if (name == kAllocatorNameBlob) {
            ba.type = "BlobAllocator";
            ba.item_size_bytes = 1;
        }
        bootstrap.allocators.push_back(std::move(ba));
    }

    std::string root_err;
    if (!image_store.saveRootState(bootstrap.roots_file,
                                   makeRootState("session", root_sid), &root_err) ||
        !image_store.saveBootstrap(bootstrap, &root_err)) {
        if (error) *error = "failed to write root anchor/bootstrap: " + root_err;
        return false;
    }
    return true;
}

bool SessionStateStore::loadRootFileBacked(CPtr<agentc::ListreeValue>& out,
                                            std::string* error) const {
    auto image_store = sessionImageStore();
    if (!image_store.exists()) {
        if (error) *error = "session state does not exist";
        return false;
    }

    // Reset all heap-side allocator state without pre-creating a heap-backed
    // slab[0] sentinel.  The sentinel lives at slab[0][0] in the saved files
    // and will be restored by reattachFileBackedSlabs.
    Allocator<agentc::ListreeValue>::getAllocator().resetHeapStateOnly();
    Allocator<agentc::ListreeValueRef>::getAllocator().resetHeapStateOnly();
    Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().resetHeapStateOnly();
    Allocator<agentc::ListreeItem>::getAllocator().resetHeapStateOnly();
    Allocator<AATree<agentc::ListreeItem>>::getAllocator().resetHeapStateOnly();
    BlobAllocator::getAllocator().clearAllSlabs();
    // Reconfigure file-backed backing (resetHeapStateOnly cleared it).
    const_cast<SessionStateStore*>(this)->configureFileBackedAllocators();

    // Re-attach slab files.  inUse arrays are read directly from the file
    // header region — no separate metadata file needed.
    std::string attach_err;
    if (!Allocator<agentc::ListreeValue>::getAllocator().reattachFileBackedSlabs(&attach_err) ||
        !Allocator<agentc::ListreeValueRef>::getAllocator().reattachFileBackedSlabs(&attach_err) ||
        !Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().reattachFileBackedSlabs(&attach_err) ||
        !Allocator<agentc::ListreeItem>::getAllocator().reattachFileBackedSlabs(&attach_err) ||
        !Allocator<AATree<agentc::ListreeItem>>::getAllocator().reattachFileBackedSlabs(&attach_err)) {
        if (error) *error = "failed to reattach slab files: " + attach_err;
        return false;
    }
    if (!BlobAllocator::getAllocator().reattachFileBackedBlobSlabs(&attach_err)) {
        if (error) *error = "failed to reattach blob slab files: " + attach_err;
        return false;
    }

    // 4. Restore slab[0] for each allocator from the persisted file.
    //    The bootstrap contains the file paths in the allocator's `files` field.
    ArenaRootState root_state;
    SessionImageBootstrap bootstrap;
    if (!image_store.loadBootstrap(bootstrap, error)) return false;

    const std::string session_dir = sessionImageStore().sessionPath();
    auto restoreSlabZero = [&](auto& allocator, const char* name) {
        // Find the allocator bootstrap entry to get its slab files.
        for (const auto& ba : bootstrap.allocators) {
            if (ba.name == name && !ba.files.empty()) {
                std::string slab0Path = (std::filesystem::path(session_dir) / ba.files[0]).string();
                // Skip if file doesn't exist (e.g., BlobAllocator with no allocations).
                if (!std::filesystem::exists(slab0Path)) break;
                std::string restore_err;
                allocator.restoreSlabFromFile(0, slab0Path, &restore_err);
                break;
            }
        }
    };
    restoreSlabZero(Allocator<agentc::ListreeValue>::getAllocator(),        kAllocatorNameValue);
    restoreSlabZero(Allocator<agentc::ListreeValueRef>::getAllocator(),     kAllocatorNameRef);
    restoreSlabZero(Allocator<CLL<agentc::ListreeValueRef>>::getAllocator(), kAllocatorNameNode);
    restoreSlabZero(Allocator<agentc::ListreeItem>::getAllocator(),         kAllocatorNameItem);
    restoreSlabZero(Allocator<AATree<agentc::ListreeItem>>::getAllocator(),  kAllocatorNameTree);
    restoreSlabZero(BlobAllocator::getAllocator(),                          kAllocatorNameBlob);

    // Look up the root anchor.
    if (!image_store.loadRootState(bootstrap.roots_file, root_state, error)) return false;

    const SlabId root_sid = findAnchor(root_state, "session");
    if (root_sid == SlabId{}) {
        if (error) *error = "missing session root anchor";
        return false;
    }

    out = CPtr<agentc::ListreeValue>(root_sid);
    if (!out) {
        if (error) *error = "restored session root is null";
        return false;
    }
    return true;
}

void SessionStateStore::clear() const {
    sessionImageStore().clear();
}

SessionImageStore SessionStateStore::sessionImageStore() const {
    return SessionImageStore(root_path_, session_name_);
}

} // namespace agentc::runtime
