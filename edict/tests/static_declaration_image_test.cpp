// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include "../static_declaration_image.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value,
                                      const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string textValue(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return {};
    }
    if ((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

size_t listCount(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->isListMode()) {
        return 0;
    }
    size_t count = 0;
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            ++count;
        }
    });
    return count;
}

void resetListreeAllocatorsForStaticDeclarationTests() {
    Allocator<agentc::ListreeValue>::getAllocator().resetForTests();
    Allocator<agentc::ListreeValueRef>::getAllocator().resetForTests();
    Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().resetForTests();
    Allocator<agentc::ListreeItem>::getAllocator().resetForTests();
    Allocator<AATree<agentc::ListreeItem>>::getAllocator().resetForTests();
}

} // namespace

TEST(StaticDeclarationImageTest, WorkerPrimitiveImageIsMetadataOnlyAndValidates) {
    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    ASSERT_TRUE(image);

    auto manifest = namedValue(image, "manifest");
    ASSERT_TRUE(manifest);
    EXPECT_EQ(textValue(namedValue(manifest, "format")), "agentc.static_declaration_image");
    EXPECT_EQ(textValue(namedValue(manifest, "format_version")), "1");
    EXPECT_EQ(textValue(namedValue(manifest, "module")), "worker.edict");
    EXPECT_EQ(textValue(namedValue(manifest, "contains_native_handles")), "false");
    EXPECT_EQ(textValue(namedValue(manifest, "native_binding_policy")), "lazy_process_local_sidecar");

    auto declarations = namedValue(image, "declarations");
    ASSERT_TRUE(declarations);
    EXPECT_GE(listCount(declarations), 6u);
    EXPECT_EQ(textValue(namedValue(manifest, "payload_hash")),
              agentc::edict::static_image::declarationPayloadHash(declarations));

    const auto validation = agentc::edict::static_image::validateDeclarationImage(image);
    EXPECT_TRUE(validation.ok) << validation.code << ": " << validation.message;
}

TEST(StaticDeclarationImageTest, WorkerPrimitiveImageRoundTripsThroughFile) {
    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    const auto path = std::filesystem::temp_directory_path() /
                      "agentc-worker-static-declaration-image-test.json";

    std::string error;
    ASSERT_TRUE(agentc::edict::static_image::writeDeclarationImage(image, path.string(), &error)) << error;
    auto restored = agentc::edict::static_image::readDeclarationImage(path.string(), &error);
    ASSERT_TRUE(restored) << error;

    const auto validation = agentc::edict::static_image::validateDeclarationImage(restored);
    EXPECT_TRUE(validation.ok) << validation.code << ": " << validation.message;
    EXPECT_EQ(agentc::toJson(restored), agentc::toJson(image));

    std::filesystem::remove(path);
}

TEST(StaticDeclarationImageTest, BinaryContainerMmapValidatesAndMountsStaticImmortal) {
    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    const auto path = std::filesystem::temp_directory_path() /
                      "agentc-worker-static-declaration-image-container-test.acsdi";

    std::string error;
    ASSERT_TRUE(agentc::edict::static_image::writeDeclarationImageContainer(image, path.string(), &error)) << error;
    auto restored = agentc::edict::static_image::readDeclarationImageContainerMmapReadOnly(path.string(), &error);
    ASSERT_TRUE(restored) << error;
    auto validation = agentc::edict::static_image::validateDeclarationImage(restored);
    ASSERT_TRUE(validation.ok) << validation.code << ": " << validation.message;

    auto mounted = agentc::edict::static_image::mountDeclarationImageReadOnly(restored);
    ASSERT_TRUE(mounted.validation.ok) << mounted.validation.code << ": " << mounted.validation.message;
    ASSERT_TRUE(mounted.root);
    EXPECT_TRUE(mounted.root->isReadOnly());
    EXPECT_FALSE(mounted.staticValueSlots.empty());
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(mounted.root.getSlabId()));

    std::filesystem::remove(path);
}

TEST(StaticDeclarationImageTest, BinaryContainerRejectsInvalidMagic) {
    const auto path = std::filesystem::temp_directory_path() /
                      "agentc-worker-static-declaration-image-bad-container-test.acsdi";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "not-a-valid-container";
    }

    std::string error;
    auto restored = agentc::edict::static_image::readDeclarationImageContainerMmapReadOnly(path.string(), &error);
    EXPECT_FALSE(restored);
    EXPECT_NE(error.find("magic"), std::string::npos) << error;

    std::filesystem::remove(path);
}

TEST(StaticDeclarationImageTest, MmapReadOnlyImageCanBeMountedStaticImmortal) {
    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    const auto path = std::filesystem::temp_directory_path() /
                      "agentc-worker-static-declaration-image-mmap-test.json";

    std::string error;
    ASSERT_TRUE(agentc::edict::static_image::writeDeclarationImage(image, path.string(), &error)) << error;
    auto restored = agentc::edict::static_image::readDeclarationImageMmapReadOnly(path.string(), &error);
    ASSERT_TRUE(restored) << error;
    EXPECT_EQ(agentc::toJson(restored), agentc::toJson(image));

    auto mounted = agentc::edict::static_image::mountDeclarationImageReadOnly(restored);
    ASSERT_TRUE(mounted.validation.ok) << mounted.validation.code << ": " << mounted.validation.message;
    ASSERT_TRUE(mounted.root);
    EXPECT_TRUE(mounted.root->isReadOnly());
    EXPECT_FALSE(mounted.staticValueSlots.empty());

    const SlabId rootSid = mounted.root.getSlabId();
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(rootSid));
    const size_t refsBefore = Allocator<agentc::ListreeValue>::getAllocator().refs(rootSid);
    const int pinsBefore = mounted.root->getPinnedCount();
    {
        CPtr<agentc::ListreeValue> copy = mounted.root;
        EXPECT_EQ(Allocator<agentc::ListreeValue>::getAllocator().refs(rootSid), refsBefore);
    }
    mounted.root->pin();
    mounted.root->unpin();
    EXPECT_EQ(Allocator<agentc::ListreeValue>::getAllocator().refs(rootSid), refsBefore);
    EXPECT_EQ(mounted.root->getPinnedCount(), pinsBefore);

    std::filesystem::remove(path);
}

TEST(StaticDeclarationImageTest, RegistryBackedMountExposesLogicalMountIdAndRootMetadata) {
    resetListreeAllocatorsForStaticDeclarationTests();

    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    const SlabId rootSid = image.getSlabId();
    agentc::ListreeStaticMountRegistry registry;
    uint64_t mountId = 0;

    {
        auto mounted = agentc::edict::static_image::mountDeclarationImageReadOnly(image, registry);
        ASSERT_TRUE(mounted.validation.ok) << mounted.validation.code << ": " << mounted.validation.message;
        ASSERT_NE(mounted.mountId, 0u);
        EXPECT_EQ(mounted.rootId, rootSid);
        EXPECT_TRUE(mounted.root->isReadOnly());
        EXPECT_TRUE(registry.active(mounted.mountId));
        EXPECT_EQ(registry.rootId(mounted.mountId), rootSid);
        const auto metadata = registry.metadata(mounted.mountId);
        EXPECT_EQ(metadata.imageId, "worker.edict:" + textValue(namedValue(namedValue(mounted.root, "manifest"), "payload_hash")));
        EXPECT_EQ(metadata.manifestHash, textValue(namedValue(namedValue(mounted.root, "manifest"), "payload_hash")));
        EXPECT_EQ(metadata.rootDescriptor, "worker.edict/declarations");
        EXPECT_EQ(metadata.sectionDescriptor, "declarative_import_module:worker.edict");
        EXPECT_EQ(metadata.provenance, "static_declaration_image");
        mountId = mounted.mountId;
    }

    image = nullptr;
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().valid(rootSid));
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(rootSid));

    {
        auto root = registry.root(mountId);
        ASSERT_TRUE(root);
        EXPECT_EQ(root.getSlabId(), rootSid);
        const auto validation = agentc::edict::static_image::validateDeclarationImage(root);
        EXPECT_TRUE(validation.ok) << validation.code << ": " << validation.message;
    }

    EXPECT_TRUE(registry.unmount(mountId));
    EXPECT_FALSE(registry.active(mountId));
    EXPECT_FALSE(bool(registry.root(mountId)));
    EXPECT_FALSE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(rootSid));

    resetListreeAllocatorsForStaticDeclarationTests();
}

TEST(StaticDeclarationImageTest, ReadOnlyMountMarksDeclarationValueSlotsStaticImmortal) {
    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    const SlabId rootSid = image.getSlabId();
    const int pinsBefore = image->getPinnedCount();

    auto mounted = agentc::edict::static_image::mountDeclarationImageReadOnly(image);
    ASSERT_TRUE(mounted.validation.ok) << mounted.validation.code << ": " << mounted.validation.message;
    ASSERT_TRUE(mounted.root);
    EXPECT_TRUE(mounted.root->isReadOnly());
    EXPECT_FALSE(mounted.staticValueSlots.empty());
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(rootSid));
    const size_t refsBefore = Allocator<agentc::ListreeValue>::getAllocator().refs(rootSid);

    {
        CPtr<agentc::ListreeValue> copyA = mounted.root;
        CPtr<agentc::ListreeValue> copyB = copyA;
        EXPECT_EQ(Allocator<agentc::ListreeValue>::getAllocator().refs(rootSid), refsBefore);
    }
    EXPECT_EQ(Allocator<agentc::ListreeValue>::getAllocator().refs(rootSid), refsBefore);

    mounted.root->pin();
    mounted.root->unpin();
    EXPECT_EQ(mounted.root->getPinnedCount(), pinsBefore);

    const auto validation = agentc::edict::static_image::validateDeclarationImage(mounted.root);
    EXPECT_TRUE(validation.ok) << validation.code << ": " << validation.message;
}

TEST(StaticDeclarationImageTest, ValidationRejectsPayloadHashMismatch) {
    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    auto manifest = namedValue(image, "manifest");
    ASSERT_TRUE(manifest);
    agentc::addNamedItem(manifest, "payload_hash", agentc::createStringValue("bad-hash"));

    const auto validation = agentc::edict::static_image::validateDeclarationImage(image);
    EXPECT_FALSE(validation.ok);
    EXPECT_EQ(validation.code, "payload_hash_mismatch");
}
