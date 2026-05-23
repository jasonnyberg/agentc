// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include "../static_slot_table_image.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

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

    bool foundActiveCount = false;
    bool foundContractValidator = false;
    for (size_t i = 0; i < view.declarationCount(); ++i) {
        if (view.declarationWord(i) == "worker.edict_active_count") {
            foundActiveCount = true;
            EXPECT_EQ(view.declarationNativeSymbol(i), "agentc_worker_edict_active_count_ltv");
            EXPECT_EQ(view.declarationStackSignature(i), "() -> ltv");
            EXPECT_EQ(view.declarationCategory(i), "lifecycle");
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
