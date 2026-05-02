#include <gtest/gtest.h>

#include "../runtime/persistence/agent_root_state.h"
#include "../runtime/persistence/session_image_store.h"
#include "../runtime/persistence/session_state_store.h"
#include "../../core/alloc.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>

namespace {

template <typename T>
agentc::runtime::SessionImageAllocatorManifest makeTestAllocatorManifest(
    const std::string& logical_name,
    const std::string& type_name,
    const std::vector<ArenaSlabImage>& images) {
    agentc::runtime::SessionImageAllocatorManifest manifest;
    manifest.name = logical_name;
    manifest.type = type_name;
    manifest.encoding = ArenaPersistenceTraits<T>::encoding == ArenaSlabEncoding::RawBytes
        ? "raw_bytes"
        : "structured";
    manifest.item_size_bytes = sizeof(T);
    manifest.metadata_file = "allocators/" + logical_name + "/meta.bin";
    for (const auto& image : images) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "slab.%04u.bin", image.slabIndex);
        agentc::runtime::SessionImageSlabFile slab_file;
        slab_file.index = image.slabIndex;
        slab_file.file = "allocators/" + logical_name + "/" + buffer;
        manifest.slabs.push_back(std::move(slab_file));
    }
    return manifest;
}

} // namespace

TEST(SessionStateStoreTest, RoundTripsCanonicalAgentRootThroughListreeBackedStore) {
    const auto base = (std::filesystem::temp_directory_path() / "agentc_session_state_test").string();
    agentc::runtime::SessionStateStore store(base);
    store.clear();

    nlohmann::json root = agentc::runtime::make_default_agent_root("persist me", "google", "gemini-2.5-flash");
    root["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "hello"}},
        nlohmann::json{{"role", "assistant"}, {"text", "world"}}
    });
    root["conversation"]["assistant_text"] = "world";
    root["loop"]["status"] = "turn-complete";

    std::string error;
    ASSERT_TRUE(store.save(root, &error)) << error;
    ASSERT_TRUE(store.exists());

    nlohmann::json restored;
    ASSERT_TRUE(store.load(restored, &error)) << error;
    ASSERT_TRUE(restored["conversation"].is_object());
    ASSERT_TRUE(restored["memory"].is_object());
    ASSERT_TRUE(restored["policy"].is_object());
    ASSERT_TRUE(restored["runtime"].is_object());
    ASSERT_TRUE(restored["loop"].is_object());
    ASSERT_EQ(restored["conversation"]["system_prompt"].get<std::string>(), "persist me");
    ASSERT_EQ(restored["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(restored["conversation"]["messages"][0]["role"].get<std::string>(), "user");
    EXPECT_EQ(restored["conversation"]["messages"][1]["text"].get<std::string>(), "world");
    EXPECT_EQ(restored["loop"]["status"].get<std::string>(), "turn-complete");

    store.clear();
    EXPECT_FALSE(store.exists());
}

TEST(SessionStateStoreTest, IsolatesNamedSessionsInSeparateSubdirectories) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_named_session_state_test").string();
    agentc::runtime::SessionStateStore alpha(root, "alpha");
    agentc::runtime::SessionStateStore beta(root, "beta");
    alpha.clear();
    beta.clear();

    nlohmann::json alphaRoot = agentc::runtime::make_default_agent_root("alpha prompt", "google", "gemini-2.5-flash");
    alphaRoot["conversation"]["assistant_text"] = "alpha reply";
    nlohmann::json betaRoot = agentc::runtime::make_default_agent_root("beta prompt", "google", "gemini-2.5-flash");
    betaRoot["conversation"]["assistant_text"] = "beta reply";

    std::string error;
    ASSERT_TRUE(alpha.save(alphaRoot, &error)) << error;
    ASSERT_TRUE(beta.save(betaRoot, &error)) << error;

    nlohmann::json restoredAlpha;
    nlohmann::json restoredBeta;
    ASSERT_TRUE(alpha.load(restoredAlpha, &error)) << error;
    ASSERT_TRUE(beta.load(restoredBeta, &error)) << error;

    EXPECT_EQ(restoredAlpha["conversation"]["system_prompt"].get<std::string>(), "alpha prompt");
    EXPECT_EQ(restoredBeta["conversation"]["system_prompt"].get<std::string>(), "beta prompt");
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(root) / "alpha" / "manifest.json"));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(root) / "beta" / "manifest.json"));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(root) / "alpha" / "roots.bin"));
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(root) / "beta" / "roots.bin"));

    alpha.clear();
    beta.clear();
}

TEST(SessionStateStoreTest, WritesSessionManifestAndAllocatorImageIndex) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_session_manifest_test").string();
    agentc::runtime::SessionStateStore store(root, "manifest-check");
    store.clear();

    nlohmann::json session_root = agentc::runtime::make_default_agent_root(
        "this is a long enough prompt to force blob-backed string storage during restore validation",
        "google",
        "gemini-2.5-flash");
    session_root["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "first message"}},
        nlohmann::json{{"role", "assistant"}, {"text", "second message"}}
    });
    session_root["memory"]["summary"] = "this summary is intentionally longer than the inline payload threshold";

    std::string error;
    ASSERT_TRUE(store.save(session_root, &error)) << error;

    const auto manifest_path = std::filesystem::path(root) / "manifest-check" / "manifest.json";
    ASSERT_TRUE(std::filesystem::exists(manifest_path));

    std::ifstream in(manifest_path);
    ASSERT_TRUE(in.good());
    nlohmann::json manifest = nlohmann::json::parse(in);
    ASSERT_EQ(manifest["version"].get<int>(), 1);
    ASSERT_EQ(manifest["session"].get<std::string>(), "manifest-check");
    ASSERT_EQ(manifest["roots_file"].get<std::string>(), "roots.bin");
    ASSERT_TRUE(manifest["allocators"].is_array());
    ASSERT_EQ(manifest["allocators"].size(), 5u);

    std::map<std::string, nlohmann::json> allocators;
    for (const auto& allocator : manifest["allocators"]) {
        allocators[allocator["name"].get<std::string>()] = allocator;
    }

    ASSERT_TRUE(allocators.count("value"));
    ASSERT_TRUE(allocators.count("ref"));
    ASSERT_TRUE(allocators.count("node"));
    ASSERT_TRUE(allocators.count("item"));
    ASSERT_TRUE(allocators.count("tree"));
    EXPECT_EQ(allocators["value"]["encoding"].get<std::string>(), "structured");
    EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(root) / "manifest-check" /
                                        allocators["value"]["metadata_file"].get<std::string>()));

    for (const auto& [name, allocator] : allocators) {
        ASSERT_TRUE(allocator["slabs"].is_array()) << name;
        for (const auto& slab : allocator["slabs"]) {
            const auto slab_path = std::filesystem::path(root) / "manifest-check" /
                                   slab["file"].get<std::string>();
            EXPECT_TRUE(std::filesystem::exists(slab_path)) << name;
            ASSERT_TRUE(slab.contains("format")) << name;
            EXPECT_EQ(slab["format"].get<std::string>(), "session_image_mmap_v1") << name;
            ASSERT_TRUE(slab.contains("payload_offset_bytes")) << name;
            ASSERT_TRUE(slab.contains("payload_size_bytes")) << name;
            const auto payload_offset = slab["payload_offset_bytes"].get<uint64_t>();
            const auto payload_size = slab["payload_size_bytes"].get<uint64_t>();
            EXPECT_GT(payload_offset, 0u) << name;
            EXPECT_GT(payload_size, 0u) << name;
            EXPECT_EQ(payload_offset % 4096u, 0u) << name;
            EXPECT_GE(std::filesystem::file_size(slab_path), payload_offset + payload_size) << name;
        }
    }

    nlohmann::json restored;
    ASSERT_TRUE(store.load(restored, &error)) << error;
    EXPECT_EQ(restored["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(restored["memory"]["summary"].get<std::string>(),
              "this summary is intentionally longer than the inline payload threshold");

    store.clear();
}

TEST(SessionStateStoreTest, CanAllocateRawSlabsFileFirstWithMmapBackingPolicy) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_file_first_raw_allocator_test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    auto& allocator = Allocator<uint64_t>::getAllocator();
    allocator.resetForTests();
    allocator.configureMmapFileBackedSlabs(root, "u64filefirst");
    allocator.setSlabBackingPolicy(ArenaSlabBackingPolicy::MmapFile);

    std::vector<SlabId> saved_ids;
    saved_ids.reserve(SLAB_SIZE + 4);
    for (uint64_t value = 1; value <= static_cast<uint64_t>(SLAB_SIZE + 4); ++value) {
        saved_ids.push_back(allocator.allocate(value * 7));
    }

    EXPECT_FALSE(allocator.slabUsesMappedBacking(0));
    ASSERT_TRUE(allocator.slabUsesMappedBacking(1));
    const auto slab0_path = allocator.slabBackingPath(0);
    const auto slab1_path = allocator.slabBackingPath(1);
    ASSERT_FALSE(slab0_path.empty());
    ASSERT_FALSE(slab1_path.empty());
    EXPECT_FALSE(std::filesystem::exists(slab0_path));
    EXPECT_TRUE(std::filesystem::exists(slab1_path));
    EXPECT_EQ(std::filesystem::file_size(slab1_path), static_cast<uint64_t>(SLAB_SIZE * sizeof(uint64_t)));

    const SlabId first_file_backed_sid = saved_ids[SLAB_SIZE - 1];
    ASSERT_EQ(first_file_backed_sid.first, 1);
    uint64_t* first_ptr = allocator.getPtr(first_file_backed_sid);
    ASSERT_NE(first_ptr, nullptr);
    EXPECT_EQ(*first_ptr, static_cast<uint64_t>(SLAB_SIZE) * 7u);
    *first_ptr = 123456u;
    ASSERT_TRUE(allocator.flushMappedSlabs());

    std::ifstream slab1_in(slab1_path, std::ios::binary);
    ASSERT_TRUE(slab1_in.good());
    slab1_in.seekg(static_cast<std::streamoff>(first_file_backed_sid.second * sizeof(uint64_t)), std::ios::beg);
    uint64_t persisted_value = 0;
    slab1_in.read(reinterpret_cast<char*>(&persisted_value), sizeof(persisted_value));
    ASSERT_TRUE(slab1_in.good());
    EXPECT_EQ(persisted_value, 123456u);

    allocator.resetForTests();
    std::filesystem::remove_all(root, ec);
}

TEST(SessionStateStoreTest, CanAttachRawAllocatorSlabsFromMappedBacking) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_mmap_attachable_raw_allocator_test").string();
    agentc::runtime::SessionImageStore image_store(root, "raw-attach");
    image_store.clear();

    auto& allocator = Allocator<uint64_t>::getAllocator();
    allocator.resetForTests();

    std::vector<SlabId> saved_ids;
    saved_ids.reserve(SLAB_SIZE + 16);
    for (uint64_t value = 1; value <= static_cast<uint64_t>(SLAB_SIZE + 16); ++value) {
        const SlabId sid = allocator.allocate(value * 10);
        saved_ids.push_back(sid);
    }
    ASSERT_EQ(saved_ids.front().first, 0);
    ASSERT_EQ(saved_ids.back().first, 1);

    const auto metadata = allocator.exportArenaMetadata();
    const auto images = allocator.exportSlabImages();
    ASSERT_FALSE(images.empty());

    auto allocator_manifest = makeTestAllocatorManifest<uint64_t>("u64", "uint64_t", images);
    agentc::runtime::SessionImageManifest manifest;
    manifest.session = image_store.sessionName();
    manifest.allocators = {allocator_manifest};

    std::string error;
    ASSERT_TRUE(image_store.saveAllocatorMetadata(allocator_manifest, metadata, &error)) << error;
    ASSERT_TRUE(image_store.saveAllocatorSlabs(allocator_manifest, images, &error)) << error;
    manifest.allocators = {allocator_manifest};
    ASSERT_TRUE(image_store.saveManifest(manifest, &error)) << error;

    agentc::runtime::SessionImageManifest loaded_manifest;
    ASSERT_TRUE(image_store.loadManifest(loaded_manifest, &error)) << error;
    ASSERT_EQ(loaded_manifest.allocators.size(), 1u);
    const auto& loaded_allocator_manifest = loaded_manifest.allocators.front();
    ASSERT_EQ(loaded_allocator_manifest.slabs.size(), images.size());
    for (const auto& slab : loaded_allocator_manifest.slabs) {
        EXPECT_EQ(slab.format, "session_image_mmap_raw_v1");
        EXPECT_GT(slab.payload_offset_bytes, 0u);
        EXPECT_EQ(slab.payload_offset_bytes % 4096u, 0u);
        EXPECT_EQ(slab.payload_size_bytes, static_cast<uint64_t>(SLAB_SIZE * sizeof(uint64_t)));
    }

    allocator.resetForTests();
    ArenaCheckpointMetadata restored_metadata;
    ASSERT_TRUE(image_store.loadAllocatorMetadata(loaded_allocator_manifest, restored_metadata, &error)) << error;
    ASSERT_TRUE(allocator.restoreArenaMetadata(restored_metadata));

    std::vector<ArenaMappedRawSlabAttachment> attachments;
    ASSERT_TRUE(image_store.loadAllocatorMappedRawSlabs(loaded_allocator_manifest, attachments, &error)) << error;
    ASSERT_EQ(attachments.size(), images.size());
    const auto first_slab_items = reinterpret_cast<uint64_t*>(attachments.front().items);
    ASSERT_NE(first_slab_items, nullptr);

    ASSERT_TRUE(allocator.attachMappedRawSlabs(attachments));

    const SlabId first_value_sid = saved_ids.front();
    const SlabId second_slab_sid = saved_ids.back();
    uint64_t* first_ptr = allocator.getPtr(first_value_sid);
    ASSERT_NE(first_ptr, nullptr);
    EXPECT_EQ(*first_ptr, 10u);
    EXPECT_EQ(first_ptr, first_slab_items + first_value_sid.second);

    uint64_t* second_ptr = allocator.getPtr(second_slab_sid);
    ASSERT_NE(second_ptr, nullptr);
    EXPECT_EQ(*second_ptr, static_cast<uint64_t>(SLAB_SIZE + 16) * 10);

    attachments.clear();
    EXPECT_EQ(*allocator.getPtr(first_value_sid), 10u);

    *first_ptr = 424242u;
    EXPECT_EQ(*(first_slab_items + first_value_sid.second), 424242u);

    allocator.resetForTests();
    image_store.clear();
}

TEST(SessionStateStoreTest, CanAttachStructuredListreeValueRefSlabsFromMappedBacking) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_mmap_attachable_structured_allocator_test").string();
    agentc::runtime::SessionImageStore image_store(root, "structured-attach");
    image_store.clear();

    auto& value_allocator = Allocator<agentc::ListreeValue>::getAllocator();
    auto& ref_allocator = Allocator<agentc::ListreeValueRef>::getAllocator();
    value_allocator.resetForTests();
    ref_allocator.resetForTests();

    auto shared_value = agentc::createStringValue("mapped-ref-value");
    ASSERT_TRUE(shared_value);

    std::vector<SlabId> saved_ids;
    saved_ids.reserve(SLAB_SIZE + 8);
    for (size_t i = 0; i < SLAB_SIZE + 8; ++i) {
        saved_ids.push_back(ref_allocator.allocate(shared_value));
    }
    ASSERT_EQ(saved_ids.front().first, 0);
    ASSERT_EQ(saved_ids.back().first, 1);

    const auto metadata = ref_allocator.exportArenaMetadata();
    const auto images = ref_allocator.exportSlabImages();
    ASSERT_FALSE(images.empty());

    auto allocator_manifest = makeTestAllocatorManifest<agentc::ListreeValueRef>(
        "ltvr", "agentc::ListreeValueRef", images);
    agentc::runtime::SessionImageManifest manifest;
    manifest.session = image_store.sessionName();

    std::string error;
    ASSERT_TRUE(image_store.saveAllocatorMetadata(allocator_manifest, metadata, &error)) << error;
    ASSERT_TRUE(image_store.saveAllocatorAttachableStructuredSlabs(allocator_manifest, images, &error)) << error;
    manifest.allocators = {allocator_manifest};
    ASSERT_TRUE(image_store.saveManifest(manifest, &error)) << error;

    agentc::runtime::SessionImageManifest loaded_manifest;
    ASSERT_TRUE(image_store.loadManifest(loaded_manifest, &error)) << error;
    ASSERT_EQ(loaded_manifest.allocators.size(), 1u);
    const auto& loaded_allocator_manifest = loaded_manifest.allocators.front();
    ASSERT_EQ(loaded_allocator_manifest.slabs.size(), images.size());
    for (const auto& slab : loaded_allocator_manifest.slabs) {
        EXPECT_EQ(slab.format, "session_image_mmap_structured_attach_v1");
        EXPECT_GT(slab.payload_offset_bytes, 0u);
        EXPECT_EQ(slab.payload_offset_bytes % 4096u, 0u);
        EXPECT_GT(slab.payload_size_bytes, 0u);
    }

    ref_allocator.resetForTests();
    ArenaCheckpointMetadata restored_metadata;
    ASSERT_TRUE(image_store.loadAllocatorMetadata(loaded_allocator_manifest, restored_metadata, &error)) << error;
    ASSERT_TRUE(ref_allocator.restoreArenaMetadata(restored_metadata));

    std::vector<ArenaMappedStructuredSlabAttachment> attachments;
    ASSERT_TRUE(image_store.loadAllocatorMappedStructuredSlabs(loaded_allocator_manifest, attachments, &error)) << error;
    ASSERT_EQ(attachments.size(), images.size());
    auto* first_attachment_items = reinterpret_cast<agentc::ListreeValueRef*>(attachments.front().items);
    ASSERT_NE(first_attachment_items, nullptr);

    ASSERT_TRUE(ref_allocator.attachMappedStructuredSlabs(attachments));

    auto* first_ref = ref_allocator.getPtr(saved_ids.front());
    ASSERT_NE(first_ref, nullptr);
    EXPECT_EQ(first_ref, first_attachment_items + saved_ids.front().second);
    auto restored_value = first_ref->getValue();
    ASSERT_TRUE(restored_value);
    EXPECT_EQ(std::string(static_cast<const char*>(restored_value->getData()), restored_value->getLength()),
              "mapped-ref-value");

    auto* last_ref = ref_allocator.getPtr(saved_ids.back());
    ASSERT_NE(last_ref, nullptr);
    auto restored_last_value = last_ref->getValue();
    ASSERT_TRUE(restored_last_value);
    EXPECT_EQ(std::string(static_cast<const char*>(restored_last_value->getData()), restored_last_value->getLength()),
              "mapped-ref-value");

    attachments.clear();
    EXPECT_EQ(std::string(static_cast<const char*>(ref_allocator.getPtr(saved_ids.front())->getValue()->getData()),
                          ref_allocator.getPtr(saved_ids.front())->getValue()->getLength()),
              "mapped-ref-value");

    ref_allocator.resetForTests();
    value_allocator.resetForTests();
    image_store.clear();
}

TEST(SessionStateStoreTest, SavesNativeSnapshotWithoutDestroyingLiveAmbientState) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_native_root_anchor_test").string();
    agentc::runtime::SessionStateStore store(root, "native-root");
    store.clear();

    std::vector<CPtr<agentc::ListreeValue>> junk_values;
    junk_values.push_back(agentc::createStringValue("junk-1"));
    junk_values.push_back(agentc::createStringValue("junk-2"));
    junk_values.push_back(agentc::createStringValue("junk-3"));

    auto root_json = agentc::runtime::make_default_agent_root("native prompt", "google", "gemini-2.5-flash");
    root_json["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "hello native"}},
        nlohmann::json{{"role", "assistant"}, {"text", "world native"}}
    });
    auto root_value = agentc::fromJson(root_json.dump());
    ASSERT_TRUE(root_value);

    const auto original_root_json = nlohmann::json::parse(agentc::toJson(root_value));

    std::string error;
    ASSERT_TRUE(store.saveRoot(root_value, &error)) << error;

    EXPECT_EQ(nlohmann::json::parse(agentc::toJson(root_value)), original_root_json);
    ASSERT_TRUE(junk_values[0]);
    EXPECT_EQ(std::string(static_cast<const char*>(junk_values[0]->getData()), junk_values[0]->getLength()), "junk-1");
    ASSERT_TRUE(junk_values[1]);
    EXPECT_EQ(std::string(static_cast<const char*>(junk_values[1]->getData()), junk_values[1]->getLength()), "junk-2");

    std::ifstream roots_in(std::filesystem::path(root) / "native-root" / "roots.bin", std::ios::binary);
    ASSERT_TRUE(roots_in.good());
    const std::string roots_payload((std::istreambuf_iterator<char>(roots_in)), std::istreambuf_iterator<char>());
    ArenaRootState root_state;
    ASSERT_TRUE(deserializeArenaRootState(roots_payload, root_state));
    ASSERT_EQ(root_state.anchors.size(), 1u);
    EXPECT_EQ(root_state.anchors[0].name, "session");

    CPtr<agentc::ListreeValue> restored_root;
    ASSERT_TRUE(store.loadRoot(restored_root, &error)) << error;
    ASSERT_TRUE(restored_root);
    EXPECT_EQ(nlohmann::json::parse(agentc::toJson(restored_root))["conversation"]["system_prompt"].get<std::string>(),
              "native prompt");

    store.clear();
}
