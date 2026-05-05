#include <gtest/gtest.h>

#include "../runtime/persistence/agent_root_vm_ops.h"
#include "../runtime/persistence/agent_root_vm_ops.h"
#include "../runtime/persistence/session_image_store.h"
#include "../runtime/persistence/session_state_store.h"
#include "../../core/alloc.h"
#include "../../edict/edict_vm.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

namespace {

agentc::runtime::VmRuntimeImportArtifacts mockRuntimeArtifacts() {
    const std::filesystem::path sourceRoot(TEST_SOURCE_DIR);
    const std::filesystem::path buildRoot(TEST_BUILD_DIR);
    return agentc::runtime::VmRuntimeImportArtifacts{
        .extensions_library_path = (buildRoot / "extensions" / "libagentc_extensions.so").string(),
        .extensions_header_path = (sourceRoot / "extensions" / "agentc_stdlib.h").string(),
        .runtime_library_path = (buildRoot / "cpp-agent" / "libagent_runtime_mock.so").string(),
        .runtime_header_path = (sourceRoot / "cpp-agent" / "include" / "agentc_runtime" / "agentc_runtime.h").string(),
        .agentc_module_path = (sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc.edict").string(),
    };
}

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
            const auto slab_rel = slab["file"].get<std::string>();
            const auto slab_path = std::filesystem::path(root) / "manifest-check" / slab_rel;
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

            agentc::runtime::SessionImageStore image_store(root, "manifest-check");
            agentc::runtime::SessionImageSlabHeaderInfo header_info;
            ASSERT_TRUE(image_store.inspectSlabFile(slab_rel, header_info, &error)) << name << ": " << error;
            EXPECT_EQ(header_info.index, slab["index"].get<uint16_t>()) << name;
            EXPECT_EQ(header_info.format, slab["format"].get<std::string>()) << name;
            EXPECT_EQ(header_info.allocator_name, allocator["name"].get<std::string>()) << name;
            EXPECT_EQ(header_info.allocator_type, allocator["type"].get<std::string>()) << name;
            EXPECT_EQ(header_info.payload_offset_bytes, payload_offset) << name;
            EXPECT_EQ(header_info.payload_size_bytes, payload_size) << name;
        }
    }

    nlohmann::json restored;
    ASSERT_TRUE(store.load(restored, &error)) << error;
    EXPECT_EQ(restored["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(restored["memory"]["summary"].get<std::string>(),
              "this summary is intentionally longer than the inline payload threshold");

    store.clear();
}

TEST(SessionStateStoreTest, WritesSessionBootstrapAndCanRestoreWithoutManifest) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_session_bootstrap_restore_test").string();
    agentc::runtime::SessionStateStore store(root, "bootstrap-authority");
    store.clear();

    nlohmann::json session_root = agentc::runtime::make_default_agent_root(
        "bootstrap-owned prompt that is long enough to exercise blob-backed string storage",
        "google",
        "gemini-2.5-flash");
    session_root["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "bootstrap first message"}},
        nlohmann::json{{"role", "assistant"}, {"text", "bootstrap second message"}}
    });
    session_root["memory"]["summary"] = "bootstrap restore should not depend on manifest presence";

    std::string error;
    ASSERT_TRUE(store.save(session_root, &error)) << error;

    const auto bootstrap_path = std::filesystem::path(root) / "bootstrap-authority" / "bootstrap.json";
    ASSERT_TRUE(std::filesystem::exists(bootstrap_path));
    std::ifstream bootstrap_in(bootstrap_path);
    ASSERT_TRUE(bootstrap_in.good());
    nlohmann::json bootstrap = nlohmann::json::parse(bootstrap_in);
    ASSERT_EQ(bootstrap["version"].get<int>(), 1);
    ASSERT_EQ(bootstrap["session"].get<std::string>(), "bootstrap-authority");
    ASSERT_EQ(bootstrap["roots_file"].get<std::string>(), "roots.bin");
    ASSERT_TRUE(bootstrap["allocators"].is_array());
    ASSERT_EQ(bootstrap["allocators"].size(), 5u);
    for (const auto& allocator : bootstrap["allocators"]) {
        EXPECT_TRUE(allocator.contains("metadata_file"));
        EXPECT_FALSE(allocator.contains("slabs"));
    }

    ASSERT_TRUE(std::filesystem::remove(std::filesystem::path(root) / "bootstrap-authority" / "manifest.json"));
    EXPECT_TRUE(store.exists());

    nlohmann::json restored;
    ASSERT_TRUE(store.load(restored, &error)) << error;
    EXPECT_EQ(restored["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(restored["conversation"]["messages"][1]["text"].get<std::string>(), "bootstrap second message");
    EXPECT_EQ(restored["memory"]["summary"].get<std::string>(),
              "bootstrap restore should not depend on manifest presence");

    store.clear();
}

TEST(SessionStateStoreTest, PersistsDeclarativeRuntimeRehydrationMetadataWithoutTransientHandles) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_runtime_rehydration_persistence_test").string();
    agentc::runtime::SessionStateStore store(root, "runtime-rehydration");
    store.clear();

    auto root_json = agentc::runtime::make_default_agent_root("persist runtime metadata", "google", "gemini-2.5-flash");
    root_json["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "hello"}},
        nlohmann::json{{"role", "assistant"}, {"text", "world"}}
    });
    std::string error;
    nlohmann::json persisted_root;
    {
        auto vm_root = agentc::fromJson(root_json.dump());
        ASSERT_TRUE(vm_root);
        agentc::edict::EdictVM vm(vm_root);
        agentc::runtime::rehydrate_vm_runtime_state(
            vm,
            "persist runtime metadata",
            nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}},
            mockRuntimeArtifacts(),
            "startup-restored",
            "agentc-config.json");
        persisted_root = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()));
    }

    ASSERT_TRUE(store.save(persisted_root, &error)) << error;

    nlohmann::json restored;
    ASSERT_TRUE(store.load(restored, &error)) << error;
    ASSERT_TRUE(restored["runtime"]["rehydration"].is_object());
    EXPECT_EQ(restored["runtime"]["rehydration"]["last_event"].get<std::string>(), "startup-restored");
    EXPECT_EQ(restored["runtime"]["rehydration"]["binding"]["module_name"].get<std::string>(), "agentc");
    EXPECT_EQ(restored["runtime"]["rehydration"]["transient_handles_persisted"].get<std::string>(),
              "not-persisted");
    EXPECT_FALSE(restored.contains("__vm_runtime_response"));
    EXPECT_FALSE(restored.contains("vm_runtime_handle"));

    store.clear();
}

TEST(SessionStateStoreTest, CanRestoreCanonicalRootWhenManifestSlabListsAreEmpty) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_session_manifest_discovery_test").string();
    agentc::runtime::SessionStateStore store(root, "header-discovery");
    store.clear();

    nlohmann::json session_root = agentc::runtime::make_default_agent_root(
        "header-discovered prompt that is long enough to exercise blob-backed string storage",
        "google",
        "gemini-2.5-flash");
    session_root["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "first message"}},
        nlohmann::json{{"role", "assistant"}, {"text", "second message"}}
    });
    session_root["memory"]["summary"] = "header discovery should still find all allocator slabs";

    std::string error;
    ASSERT_TRUE(store.save(session_root, &error)) << error;

    const auto manifest_path = std::filesystem::path(root) / "header-discovery" / "manifest.json";
    ASSERT_TRUE(std::filesystem::exists(manifest_path));
    std::ifstream in(manifest_path);
    ASSERT_TRUE(in.good());
    nlohmann::json manifest = nlohmann::json::parse(in);
    for (auto& allocator : manifest["allocators"]) {
        allocator["slabs"] = nlohmann::json::array();
    }
    {
        std::ofstream out(manifest_path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.good());
        out << manifest.dump(2);
        ASSERT_TRUE(out.good());
    }

    nlohmann::json restored;
    ASSERT_TRUE(store.load(restored, &error)) << error;
    EXPECT_EQ(restored["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(restored["conversation"]["messages"][1]["text"].get<std::string>(), "second message");
    EXPECT_EQ(restored["memory"]["summary"].get<std::string>(),
              "header discovery should still find all allocator slabs");

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
    EXPECT_EQ(std::filesystem::file_size(slab1_path), static_cast<uint64_t>(SLAB_SIZE * (sizeof(size_t) + sizeof(uint64_t))));

    const SlabId first_file_backed_sid = saved_ids[SLAB_SIZE - 1];
    ASSERT_EQ(first_file_backed_sid.first, 1);
    uint64_t* first_ptr = allocator.getPtr(first_file_backed_sid);
    ASSERT_NE(first_ptr, nullptr);
    EXPECT_EQ(*first_ptr, static_cast<uint64_t>(SLAB_SIZE) * 7u);
    *first_ptr = 123456u;
    ASSERT_TRUE(allocator.flushMappedSlabs());

    std::ifstream slab1_in(slab1_path, std::ios::binary);
    ASSERT_TRUE(slab1_in.good());
    slab1_in.seekg(static_cast<std::streamoff>(
        SLAB_SIZE * sizeof(size_t) +              // skip the inUse prefix
        first_file_backed_sid.second * sizeof(uint64_t)), std::ios::beg);
    uint64_t persisted_value = 0;
    slab1_in.read(reinterpret_cast<char*>(&persisted_value), sizeof(persisted_value));
    ASSERT_TRUE(slab1_in.good());
    EXPECT_EQ(persisted_value, 123456u);

    allocator.resetForTests();
    std::filesystem::remove_all(root, ec);
}

TEST(SessionStateStoreTest, CanAllocateStructuredListreeValueSlabsFileFirstWithMmapBackingPolicy) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_file_first_value_allocator_test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    auto& value_allocator = Allocator<agentc::ListreeValue>::getAllocator();
    value_allocator.resetForTests();
    value_allocator.configureMmapFileBackedSlabs(root, "valuefilefirst");
    value_allocator.setSlabBackingPolicy(ArenaSlabBackingPolicy::MmapFile);

    std::vector<SlabId> saved_ids;
    saved_ids.reserve(SLAB_SIZE + 4);
    for (size_t i = 0; i < SLAB_SIZE + 4; ++i) {
        saved_ids.push_back(value_allocator.allocate(std::string("vv"), agentc::LtvFlags::Duplicate));
    }

    EXPECT_FALSE(value_allocator.slabUsesMappedBacking(0));
    ASSERT_TRUE(value_allocator.slabUsesMappedBacking(1));
    const auto slab0_path = value_allocator.slabBackingPath(0);
    const auto slab1_path = value_allocator.slabBackingPath(1);
    ASSERT_FALSE(slab0_path.empty());
    ASSERT_FALSE(slab1_path.empty());
    EXPECT_FALSE(std::filesystem::exists(slab0_path));
    EXPECT_TRUE(std::filesystem::exists(slab1_path));
    EXPECT_EQ(std::filesystem::file_size(slab1_path), static_cast<uint64_t>(SLAB_SIZE * (sizeof(size_t) + sizeof(agentc::ListreeValue))));

    const SlabId first_file_backed_sid = saved_ids[SLAB_SIZE - 1];
    ASSERT_EQ(first_file_backed_sid.first, 1);
    auto* first_value = value_allocator.getPtr(first_file_backed_sid);
    ASSERT_NE(first_value, nullptr);
    EXPECT_EQ(std::string(static_cast<const char*>(first_value->getData()), first_value->getLength()), "vv");
    ASSERT_TRUE(value_allocator.flushMappedSlabs());

    value_allocator.resetForTests();
    std::filesystem::remove_all(root, ec);
}

TEST(SessionStateStoreTest, CanAllocateStructuredListreeValueRefSlabsFileFirstWithMmapBackingPolicy) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_file_first_ref_allocator_test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    auto& value_allocator = Allocator<agentc::ListreeValue>::getAllocator();
    auto& ref_allocator = Allocator<agentc::ListreeValueRef>::getAllocator();
    value_allocator.resetForTests();
    ref_allocator.resetForTests();

    ref_allocator.configureMmapFileBackedSlabs(root, "reffilefirst");
    ref_allocator.setSlabBackingPolicy(ArenaSlabBackingPolicy::MmapFile);

    auto shared_value = agentc::createStringValue("mapped-ref-value");
    ASSERT_TRUE(shared_value);

    std::vector<SlabId> saved_ids;
    saved_ids.reserve(SLAB_SIZE + 4);
    for (size_t i = 0; i < SLAB_SIZE + 4; ++i) {
        saved_ids.push_back(ref_allocator.allocate(shared_value));
    }

    EXPECT_FALSE(ref_allocator.slabUsesMappedBacking(0));
    ASSERT_TRUE(ref_allocator.slabUsesMappedBacking(1));
    const auto slab0_path = ref_allocator.slabBackingPath(0);
    const auto slab1_path = ref_allocator.slabBackingPath(1);
    ASSERT_FALSE(slab0_path.empty());
    ASSERT_FALSE(slab1_path.empty());
    EXPECT_FALSE(std::filesystem::exists(slab0_path));
    EXPECT_TRUE(std::filesystem::exists(slab1_path));
    EXPECT_EQ(std::filesystem::file_size(slab1_path), static_cast<uint64_t>(SLAB_SIZE * (sizeof(size_t) + sizeof(agentc::ListreeValueRef))));

    const SlabId first_file_backed_sid = saved_ids[SLAB_SIZE - 1];
    ASSERT_EQ(first_file_backed_sid.first, 1);
    auto* first_ref = ref_allocator.getPtr(first_file_backed_sid);
    ASSERT_NE(first_ref, nullptr);
    auto restored_value = first_ref->getValue();
    ASSERT_TRUE(restored_value);
    EXPECT_EQ(std::string(static_cast<const char*>(restored_value->getData()), restored_value->getLength()),
              "mapped-ref-value");
    ASSERT_TRUE(ref_allocator.flushMappedSlabs());

    ref_allocator.resetForTests();
    value_allocator.resetForTests();
    std::filesystem::remove_all(root, ec);
}

TEST(SessionStateStoreTest, CanAllocateBlobSlabsFileFirstWithMmapBackingPolicy) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_file_first_blob_allocator_test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    auto& blob_allocator = BlobAllocator::getAllocator();
    blob_allocator.resetForTests();
    blob_allocator.configureMmapFileBackedSlabs(root, "blobfilefirst");
    blob_allocator.setSlabBackingPolicy(ArenaSlabBackingPolicy::MmapFile);

    std::string large_payload(65535, 'a');
    const SlabId first = blob_allocator.allocate(large_payload.data(), large_payload.size());
    ASSERT_EQ(first.first, 0);
    ASSERT_EQ(first.second, 1);
    std::string small_payload = "blob-second-slab";
    const SlabId second = blob_allocator.allocate(small_payload.data(), small_payload.size());
    ASSERT_EQ(second.first, 1);
    ASSERT_EQ(second.second, 0);

    ASSERT_TRUE(blob_allocator.slabUsesMappedBacking(0));
    ASSERT_TRUE(blob_allocator.slabUsesMappedBacking(1));
    const auto slab0_path = blob_allocator.slabBackingPath(0);
    const auto slab1_path = blob_allocator.slabBackingPath(1);
    ASSERT_FALSE(slab0_path.empty());
    ASSERT_FALSE(slab1_path.empty());
    EXPECT_TRUE(std::filesystem::exists(slab0_path));
    EXPECT_TRUE(std::filesystem::exists(slab1_path));
    EXPECT_EQ(std::filesystem::file_size(slab0_path), sizeof(size_t) + 65536u);
    EXPECT_EQ(std::filesystem::file_size(slab1_path), sizeof(size_t) + 65536u);

    auto* blob_ptr = static_cast<const char*>(blob_allocator.getPointer(second));
    ASSERT_NE(blob_ptr, nullptr);
    EXPECT_EQ(std::string(blob_ptr, small_payload.size()), small_payload);
    ASSERT_TRUE(blob_allocator.flushMappedSlabs());

    std::ifstream slab1_in(slab1_path, std::ios::binary);
    ASSERT_TRUE(slab1_in.good());
    slab1_in.seekg(static_cast<std::streamoff>(sizeof(size_t) + second.second), std::ios::beg);
    std::string persisted(small_payload.size(), '\0');
    slab1_in.read(persisted.data(), static_cast<std::streamsize>(persisted.size()));
    ASSERT_TRUE(slab1_in.good());
    EXPECT_EQ(persisted, small_payload);

    blob_allocator.resetForTests();
    std::filesystem::remove_all(root, ec);
}



TEST(SessionStateStoreTest, CanAllocateStructuredCllSlabsFileFirstWithMmapBackingPolicy) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_file_first_cll_allocator_test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    auto& value_allocator = Allocator<agentc::ListreeValue>::getAllocator();
    auto& ref_allocator = Allocator<agentc::ListreeValueRef>::getAllocator();
    auto& node_allocator = Allocator<CLL<agentc::ListreeValueRef>>::getAllocator();
    value_allocator.resetForTests();
    ref_allocator.resetForTests();
    node_allocator.resetForTests();

    node_allocator.configureMmapFileBackedSlabs(root, "cllfilefirst");
    node_allocator.setSlabBackingPolicy(ArenaSlabBackingPolicy::MmapFile);

    auto shared_value = agentc::createStringValue("mapped-node-value");
    ASSERT_TRUE(shared_value);
    CPtr<agentc::ListreeValueRef> shared_ref;
    {
        SlabId ref_sid = ref_allocator.allocate(shared_value);
        shared_ref = CPtr<agentc::ListreeValueRef>(ref_sid);
    }
    ASSERT_TRUE(shared_ref);

    std::vector<SlabId> saved_ids;
    saved_ids.reserve(SLAB_SIZE + 4);
    for (size_t i = 0; i < SLAB_SIZE + 4; ++i) {
        saved_ids.push_back(node_allocator.allocate(shared_ref));
    }

    EXPECT_FALSE(node_allocator.slabUsesMappedBacking(0));
    ASSERT_TRUE(node_allocator.slabUsesMappedBacking(1));
    const auto slab0_path = node_allocator.slabBackingPath(0);
    const auto slab1_path = node_allocator.slabBackingPath(1);
    ASSERT_FALSE(slab0_path.empty());
    ASSERT_FALSE(slab1_path.empty());
    EXPECT_FALSE(std::filesystem::exists(slab0_path));
    EXPECT_TRUE(std::filesystem::exists(slab1_path));
    EXPECT_EQ(std::filesystem::file_size(slab1_path), static_cast<uint64_t>(SLAB_SIZE * (sizeof(size_t) + sizeof(CLL<agentc::ListreeValueRef>))));

    const SlabId first_file_backed_sid = saved_ids[SLAB_SIZE - 1];
    ASSERT_EQ(first_file_backed_sid.first, 1);
    auto* first_node = node_allocator.getPtr(first_file_backed_sid);
    ASSERT_NE(first_node, nullptr);
    ASSERT_TRUE(first_node->data);
    auto restored_value = first_node->data->getValue();
    ASSERT_TRUE(restored_value);
    EXPECT_EQ(std::string(static_cast<const char*>(restored_value->getData()), restored_value->getLength()),
              "mapped-node-value");
    ASSERT_TRUE(node_allocator.flushMappedSlabs());

    node_allocator.resetForTests();
    ref_allocator.resetForTests();
    value_allocator.resetForTests();
    std::filesystem::remove_all(root, ec);
}

TEST(SessionStateStoreTest, CanAllocateStructuredListreeItemSlabsFileFirstWithMmapBackingPolicy) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_file_first_item_allocator_test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    auto& node_allocator = Allocator<CLL<agentc::ListreeValueRef>>::getAllocator();
    auto& item_allocator = Allocator<agentc::ListreeItem>::getAllocator();
    node_allocator.resetForTests();
    item_allocator.resetForTests();

    item_allocator.configureMmapFileBackedSlabs(root, "itemfilefirst");
    item_allocator.setSlabBackingPolicy(ArenaSlabBackingPolicy::MmapFile);

    std::vector<SlabId> saved_ids;
    saved_ids.reserve(SLAB_SIZE + 4);
    for (size_t i = 0; i < SLAB_SIZE + 4; ++i) {
        saved_ids.push_back(item_allocator.allocate(std::string("nm")));
    }

    EXPECT_FALSE(item_allocator.slabUsesMappedBacking(0));
    ASSERT_TRUE(item_allocator.slabUsesMappedBacking(1));
    const auto slab0_path = item_allocator.slabBackingPath(0);
    const auto slab1_path = item_allocator.slabBackingPath(1);
    ASSERT_FALSE(slab0_path.empty());
    ASSERT_FALSE(slab1_path.empty());
    EXPECT_FALSE(std::filesystem::exists(slab0_path));
    EXPECT_TRUE(std::filesystem::exists(slab1_path));
    EXPECT_EQ(std::filesystem::file_size(slab1_path), static_cast<uint64_t>(SLAB_SIZE * (sizeof(size_t) + sizeof(agentc::ListreeItem))));

    const SlabId first_file_backed_sid = saved_ids[SLAB_SIZE - 1];
    ASSERT_EQ(first_file_backed_sid.first, 1);
    auto* first_item = item_allocator.getPtr(first_file_backed_sid);
    ASSERT_NE(first_item, nullptr);
    EXPECT_EQ(first_item->getName(), "nm");
    EXPECT_FALSE(first_item->getValue(false, false));

    ASSERT_TRUE(item_allocator.flushMappedSlabs());

    item_allocator.resetForTests();
    node_allocator.resetForTests();
    std::filesystem::remove_all(root, ec);
}

TEST(SessionStateStoreTest, CanAllocateStructuredAATreeSlabsFileFirstWithMmapBackingPolicy) {
    const auto root = (std::filesystem::temp_directory_path() / "agentc_file_first_tree_allocator_test").string();
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    ASSERT_FALSE(ec);

    auto& item_allocator = Allocator<agentc::ListreeItem>::getAllocator();
    auto& tree_allocator = Allocator<AATree<agentc::ListreeItem>>::getAllocator();
    item_allocator.resetForTests();
    tree_allocator.resetForTests();

    tree_allocator.configureMmapFileBackedSlabs(root, "treefilefirst");
    tree_allocator.setSlabBackingPolicy(ArenaSlabBackingPolicy::MmapFile);

    CPtr<AATree<agentc::ListreeItem>> root_node;
    {
        SlabId sid = tree_allocator.allocate();
        root_node = CPtr<AATree<agentc::ListreeItem>>(sid);
    }
    ASSERT_TRUE(root_node);
    root_node->name = "root";
    {
        SlabId item_sid = item_allocator.allocate(std::string("root-item"));
        root_node->data = CPtr<agentc::ListreeItem>(item_sid);
    }

    for (size_t i = 1; i < SLAB_SIZE; ++i) {
        tree_allocator.allocate();
    }

    EXPECT_FALSE(tree_allocator.slabUsesMappedBacking(0));
    auto child_item = CPtr<agentc::ListreeItem>(item_allocator.allocate(std::string("child-item")));
    root_node->add("child", child_item);

    auto child_node = root_node->find("child");
    ASSERT_TRUE(child_node);
    EXPECT_EQ(child_node.getSlabId().first, 1);
    ASSERT_TRUE(tree_allocator.slabUsesMappedBacking(1));
    const auto slab0_path = tree_allocator.slabBackingPath(0);
    const auto slab1_path = tree_allocator.slabBackingPath(1);
    ASSERT_FALSE(slab0_path.empty());
    ASSERT_FALSE(slab1_path.empty());
    EXPECT_FALSE(std::filesystem::exists(slab0_path));
    EXPECT_TRUE(std::filesystem::exists(slab1_path));
    EXPECT_EQ(std::filesystem::file_size(slab1_path), static_cast<uint64_t>(SLAB_SIZE * (sizeof(size_t) + sizeof(AATree<agentc::ListreeItem>))));

    EXPECT_EQ(child_node->name, "child");
    ASSERT_TRUE(child_node->data);
    EXPECT_EQ(child_node->data->getName(), "child-item");
    EXPECT_EQ(child_node->level, 1u);
    ASSERT_TRUE(tree_allocator.flushMappedSlabs());

    tree_allocator.resetForTests();
    item_allocator.resetForTests();
    std::filesystem::remove_all(root, ec);
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
