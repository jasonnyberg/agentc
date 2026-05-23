// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include "../static_slot_table_image.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct TestValueRecord {
    uint32_t kind = 0;
    uint32_t stringId = 0;
    uint32_t first = 0;
    uint32_t count = 0;
};

struct TestTreeRecord {
    uint32_t firstItem = 0;
    uint32_t itemCount = 0;
};

struct TestItemRecord {
    uint32_t name = 0;
    uint32_t value = 0;
};

void appendU32(std::string& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
    }
}

void appendU64(std::string& out, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
    }
}

std::string fnv1a64(const std::string& bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char ch : bytes) {
        hash ^= static_cast<uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

void writeTable(const std::filesystem::path& path,
                const std::vector<std::string>& strings,
                const std::vector<TestValueRecord>& values,
                const std::vector<TestTreeRecord>& trees,
                const std::vector<TestItemRecord>& items,
                const std::vector<uint32_t>& listEntries,
                uint32_t rootValue = 0,
                uint32_t declarationCount = 0) {
    std::string stringRecords;
    std::string stringBytes;
    for (const auto& text : strings) {
        appendU64(stringRecords, static_cast<uint64_t>(stringBytes.size()));
        appendU64(stringRecords, static_cast<uint64_t>(text.size()));
        stringBytes.append(text);
    }

    std::string declarationRecords;
    for (uint32_t i = 0; i < declarationCount; ++i) {
        for (int field = 0; field < 8; ++field) {
            appendU32(declarationRecords, 0);
        }
    }

    std::string valueRecords;
    for (const auto& value : values) {
        appendU32(valueRecords, value.kind);
        appendU32(valueRecords, value.stringId);
        appendU32(valueRecords, value.first);
        appendU32(valueRecords, value.count);
    }

    std::string treeRecords;
    for (const auto& tree : trees) {
        appendU32(treeRecords, tree.firstItem);
        appendU32(treeRecords, tree.itemCount);
    }

    std::string itemRecords;
    for (const auto& item : items) {
        appendU32(itemRecords, item.name);
        appendU32(itemRecords, item.value);
    }

    std::string listEntryRecords;
    for (const auto& entry : listEntries) {
        appendU32(listEntryRecords, entry);
    }

    const std::string body = stringRecords + declarationRecords + valueRecords + treeRecords + itemRecords + listEntryRecords + stringBytes;
    std::string out;
    out.append("ACSTBL01", 8);
    appendU32(out, 2);
    appendU32(out, 0);
    appendU32(out, rootValue);
    appendU32(out, static_cast<uint32_t>(strings.size()));
    appendU32(out, declarationCount);
    appendU32(out, static_cast<uint32_t>(values.size()));
    appendU32(out, static_cast<uint32_t>(trees.size()));
    appendU32(out, static_cast<uint32_t>(items.size()));
    appendU32(out, static_cast<uint32_t>(listEntries.size()));
    appendU64(out, static_cast<uint64_t>(stringRecords.size()));
    appendU64(out, static_cast<uint64_t>(declarationRecords.size()));
    appendU64(out, static_cast<uint64_t>(valueRecords.size()));
    appendU64(out, static_cast<uint64_t>(treeRecords.size()));
    appendU64(out, static_cast<uint64_t>(itemRecords.size()));
    appendU64(out, static_cast<uint64_t>(listEntryRecords.size()));
    appendU64(out, static_cast<uint64_t>(stringBytes.size()));
    out.append(fnv1a64(body));
    out.append(body);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
}

} // namespace

TEST(StaticSlotTableImageTest, MmapViewInspectsWorkerDeclarationsWithoutListreeHandles) {
    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    const auto path = std::filesystem::temp_directory_path() /
                      "agentc-worker-static-slot-table-image-test.acstbl";

    std::string error;
    ASSERT_TRUE(agentc::edict::static_image::writeStaticSlotTableImage(image, path.string(), &error)) << error;
    auto view = agentc::edict::static_image::readStaticSlotTableImageMmapReadOnly(path.string(), &error);
    ASSERT_TRUE(view.ok()) << view.validation().code << ": " << view.validation().message;

    EXPECT_EQ(view.moduleName(), "worker.edict");
    EXPECT_GE(view.stringCount(), 6u);
    ASSERT_GE(view.declarationCount(), 6u);
    EXPECT_GT(view.valueCount(), view.declarationCount());
    EXPECT_EQ(view.valueKind(view.rootValueId()), agentc::edict::static_image::StaticSlotValueKind::List);
    EXPECT_EQ(view.listValueCount(view.rootValueId()), view.declarationCount());

    const int64_t activeCountIndex = view.findDeclarationByWord("worker.edict_active_count");
    ASSERT_GE(activeCountIndex, 0);
    EXPECT_EQ(view.declarationNativeSymbol(static_cast<size_t>(activeCountIndex)), "agentc_worker_edict_active_count_ltv");
    EXPECT_EQ(view.findDeclarationByWord("worker.edict_missing"), -1);

    bool foundActiveCount = false;
    bool foundContractValidator = false;
    for (size_t i = 0; i < view.declarationCount(); ++i) {
        if (view.declarationWord(i) == "worker.edict_active_count") {
            foundActiveCount = true;
            EXPECT_EQ(view.declarationNativeSymbol(i), "agentc_worker_edict_active_count_ltv");
            EXPECT_EQ(view.declarationStackSignature(i), "() -> ltv");
            EXPECT_EQ(view.declarationCategory(i), "lifecycle");
            const uint32_t declarationObject = view.declarationValueId(i);
            EXPECT_EQ(view.valueKind(declarationObject), agentc::edict::static_image::StaticSlotValueKind::Object);
            EXPECT_EQ(view.objectStringField(declarationObject, "word"), "worker.edict_active_count");
            EXPECT_EQ(view.objectStringField(declarationObject, "native_symbol"), "agentc_worker_edict_active_count_ltv");
            EXPECT_EQ(view.objectStringField(declarationObject, "stores_native_handle"), "false");
        }
        if (view.declarationWord(i) == "worker.edict_validate_result_contract") {
            foundContractValidator = true;
            EXPECT_EQ(view.declarationNativeSymbol(i), "agentc_worker_edict_validate_result_contract_ltv");
        }
    }
    EXPECT_TRUE(foundActiveCount);
    EXPECT_TRUE(foundContractValidator);

    std::filesystem::remove(path);
}

TEST(StaticSlotTableImageTest, RejectsInvalidMagic) {
    const auto path = std::filesystem::temp_directory_path() /
                      "agentc-worker-static-slot-table-bad-magic-test.acstbl";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "not-a-static-slot-table";
    }

    std::string error;
    auto view = agentc::edict::static_image::readStaticSlotTableImageMmapReadOnly(path.string(), &error);
    EXPECT_FALSE(view.ok());
    EXPECT_EQ(view.validation().code, "invalid_magic");
    EXPECT_NE(error.find("magic"), std::string::npos) << error;

    std::filesystem::remove(path);
}

TEST(StaticSlotTableImageTest, RejectsInvalidValueTreeAndListReferences) {
    const auto base = std::filesystem::temp_directory_path();
    const std::vector<std::string> strings = {"worker.edict", "field", "value"};

    {
        const auto path = base / "agentc-worker-static-slot-table-invalid-value-reference.acstbl";
        writeTable(path,
                   strings,
                   {{3, 0, 0, 0}, {2, 0, 99, 1}},
                   {},
                   {},
                   {});
        std::string error;
        auto view = agentc::edict::static_image::readStaticSlotTableImageMmapReadOnly(path.string(), &error);
        EXPECT_FALSE(view.ok());
        EXPECT_EQ(view.validation().code, "invalid_value_reference");
        std::filesystem::remove(path);
    }

    {
        const auto path = base / "agentc-worker-static-slot-table-invalid-tree-record.acstbl";
        writeTable(path,
                   strings,
                   {{3, 0, 0, 0}},
                   {{1, 1}},
                   {},
                   {});
        std::string error;
        auto view = agentc::edict::static_image::readStaticSlotTableImageMmapReadOnly(path.string(), &error);
        EXPECT_FALSE(view.ok());
        EXPECT_EQ(view.validation().code, "invalid_tree_record");
        std::filesystem::remove(path);
    }

    {
        const auto path = base / "agentc-worker-static-slot-table-invalid-list-entry.acstbl";
        writeTable(path,
                   strings,
                   {{3, 0, 0, 1}},
                   {},
                   {},
                   {99});
        std::string error;
        auto view = agentc::edict::static_image::readStaticSlotTableImageMmapReadOnly(path.string(), &error);
        EXPECT_FALSE(view.ok());
        EXPECT_EQ(view.validation().code, "invalid_list_entry");
        std::filesystem::remove(path);
    }
}

TEST(StaticSlotTableImageTest, RejectsPayloadHashMismatch) {
    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    const auto path = std::filesystem::temp_directory_path() /
                      "agentc-worker-static-slot-table-bad-hash-test.acstbl";

    std::string error;
    ASSERT_TRUE(agentc::edict::static_image::writeStaticSlotTableImage(image, path.string(), &error)) << error;

    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file);
    file.seekp(-1, std::ios::end);
    char last = 0;
    file.read(&last, 1);
    file.clear();
    file.seekp(-1, std::ios::end);
    file.put(last == 'x' ? 'y' : 'x');
    file.close();

    auto view = agentc::edict::static_image::readStaticSlotTableImageMmapReadOnly(path.string(), &error);
    EXPECT_FALSE(view.ok());
    EXPECT_EQ(view.validation().code, "payload_hash_mismatch");

    std::filesystem::remove(path);
}
