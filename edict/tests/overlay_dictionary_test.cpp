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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../edict_compiler.h"
#include "../edict_vm.h"

#ifndef TEST_EDICT_SOURCE_DIR
#define TEST_EDICT_SOURCE_DIR "."
#endif

using agentc::ListreeValue;
using agentc::edict::EdictCompiler;
using agentc::edict::EdictVM;
using namespace agentc::edict;

namespace {

CPtr<ListreeValue> namedValue(CPtr<ListreeValue> value, const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string textValue(CPtr<ListreeValue> value) {
    if (!value || !value->getData()) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

std::vector<std::string> listStrings(CPtr<ListreeValue> value) {
    std::vector<std::string> out;
    if (!value || !value->isListMode()) {
        return out;
    }
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue() && ref->getValue()->getData()) {
            out.emplace_back(static_cast<const char*>(ref->getValue()->getData()), ref->getValue()->getLength());
        }
    });
    std::reverse(out.begin(), out.end());
    return out;
}

void executeScript(EdictVM& vm, const std::string& script) {
    EdictCompiler compiler;
    const int state = vm.execute(compiler.compile(script));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
}

} // namespace

TEST(OverlayDictionaryTest, OverlayGetReturnsShadowValueOverSharedValue) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);

    executeScript(vm, R"(
        {"model": "gpt-4", "temperature": "0.7", "max_tokens": "2048"} @config
        config freeze! @frozen_config
        frozen_config overlay.new! @worker_view
        worker_view "temperature" "0.2" overlay.set! @worker_view
        worker_view "temperature" overlay.get! @temp
    )");

    EXPECT_EQ(textValue(namedValue(root, "temp")), "0.2");
}

TEST(OverlayDictionaryTest, OverlayGetFallsThroughToSharedForUnshadowedKey) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);

    executeScript(vm, R"(
        {"model": "gpt-4", "temperature": "0.7", "max_tokens": "2048"} @config
        config freeze! @frozen_config
        frozen_config overlay.new! @worker_view
        worker_view "temperature" "0.2" overlay.set! @worker_view
        worker_view "model" overlay.get! @model
        worker_view "max_tokens" overlay.get! @max_tokens
    )");

    EXPECT_EQ(textValue(namedValue(root, "model")), "gpt-4");
    EXPECT_EQ(textValue(namedValue(root, "max_tokens")), "2048");
}

TEST(OverlayDictionaryTest, CoordinatorStateUnchangedAfterShadowMutation) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);

    executeScript(vm, R"(
        {"model": "gpt-4", "temperature": "0.7", "max_tokens": "2048"} @config
        config freeze! @frozen_config
        frozen_config overlay.new! @worker_view
        worker_view "temperature" "0.2" overlay.set! @worker_view
        worker_view "temperature" overlay.get! @worker_temp
        frozen_config.temperature @coordinator_temp
    )");

    EXPECT_EQ(textValue(namedValue(root, "worker_temp")), "0.2");
    EXPECT_EQ(textValue(namedValue(root, "coordinator_temp")), "0.7");
    auto frozenConfig = namedValue(root, "frozen_config");
    ASSERT_TRUE(frozenConfig);
    EXPECT_TRUE(frozenConfig->isReadOnly());
}

TEST(OverlayDictionaryTest, OverlayHasChecksBothShadowAndShared) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);

    executeScript(vm, R"(
        {"model": "gpt-4", "temperature": "0.7"} @config
        config freeze! @frozen_config
        frozen_config overlay.new! @worker_view
        worker_view "temperature" "0.2" overlay.set! @worker_view
        worker_view "temperature" overlay.has! @has_temp
        worker_view "model" overlay.has! @has_model
        worker_view "missing_key" overlay.has! @has_missing
    )");

    auto hasTemp = namedValue(root, "has_temp");
    ASSERT_TRUE(hasTemp);
    EXPECT_TRUE(hasTemp->isListMode());
    EXPECT_EQ(listStrings(hasTemp), std::vector<std::string>({"ok"}));

    auto hasModel = namedValue(root, "has_model");
    ASSERT_TRUE(hasModel);
    EXPECT_TRUE(hasModel->isListMode());
    EXPECT_EQ(listStrings(hasModel), std::vector<std::string>({"ok"}));

    auto hasMissing = namedValue(root, "has_missing");
    ASSERT_TRUE(hasMissing);
    EXPECT_FALSE(hasMissing->isListMode());
}

TEST(OverlayDictionaryTest, OverlayKeysReturnsMergedKeyList) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);

    executeScript(vm, R"(
        {"model": "gpt-4", "temperature": "0.7", "max_tokens": "2048"} @config
        config freeze! @frozen_config
        frozen_config overlay.new! @worker_view
        worker_view "temperature" "0.2" overlay.set! @worker_view
        worker_view "custom_key" "custom_value" overlay.set! @worker_view
        worker_view overlay.keys! @all_keys
        worker_view overlay.shadow_keys! @shadow_only
    )");

    auto allKeys = listStrings(namedValue(root, "all_keys"));
    ASSERT_EQ(allKeys.size(), 4u);
    EXPECT_NE(std::find(allKeys.begin(), allKeys.end(), "temperature"), allKeys.end());
    EXPECT_NE(std::find(allKeys.begin(), allKeys.end(), "model"), allKeys.end());
    EXPECT_NE(std::find(allKeys.begin(), allKeys.end(), "max_tokens"), allKeys.end());
    EXPECT_NE(std::find(allKeys.begin(), allKeys.end(), "custom_key"), allKeys.end());

    auto shadowKeys = listStrings(namedValue(root, "shadow_only"));
    ASSERT_EQ(shadowKeys.size(), 2u);
    EXPECT_NE(std::find(shadowKeys.begin(), shadowKeys.end(), "temperature"), shadowKeys.end());
    EXPECT_NE(std::find(shadowKeys.begin(), shadowKeys.end(), "custom_key"), shadowKeys.end());
}

TEST(OverlayDictionaryTest, OverlayCommitExtractsShadowsForCoordinatorInspection) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);

    executeScript(vm, R"(
        {"model": "gpt-4", "temperature": "0.7"} @config
        config freeze! @frozen_config
        frozen_config overlay.new! @worker_view
        worker_view "temperature" "0.2" overlay.set! @worker_view
        worker_view "custom_key" "custom_value" overlay.set! @worker_view
        worker_view overlay.commit! @worker_diff
    )");

    auto workerDiff = namedValue(root, "worker_diff");
    ASSERT_TRUE(workerDiff);
    EXPECT_EQ(textValue(namedValue(workerDiff, "temperature")), "0.2");
    EXPECT_EQ(textValue(namedValue(workerDiff, "custom_key")), "custom_value");
    EXPECT_FALSE(namedValue(workerDiff, "model"));
}

TEST(OverlayDictionaryTest, OverlaySurvivesSerializationRoundTrip) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);

    executeScript(vm, R"(
        {"model": "gpt-4", "temperature": "0.7"} @config
        config freeze! @frozen_config
        frozen_config overlay.new! @worker_view
        worker_view "temperature" "0.2" overlay.set! @worker_view
        worker_view overlay.commit! @shadows
        shadows to_json! @shadows_json
        shadows_json from_json! @restored_shadows
        restored_shadows.temperature @restored_temp
    )");

    EXPECT_EQ(textValue(namedValue(root, "restored_temp")), "0.2");
    const std::string json = textValue(namedValue(root, "shadows_json"));
    EXPECT_NE(json.find("temperature"), std::string::npos);
    EXPECT_NE(json.find("0.2"), std::string::npos);
}

TEST(OverlayDictionaryTest, OverlayDoesNotWeakenReadOnlyGuarantees) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);

    executeScript(vm, R"(
        {"model": "gpt-4", "temperature": "0.7"} @config
        config freeze! @frozen_config
        frozen_config overlay.new! @worker_view
    )");

    auto frozenConfig = namedValue(root, "frozen_config");
    ASSERT_TRUE(frozenConfig);
    EXPECT_TRUE(frozenConfig->isReadOnly());

    auto overlay = namedValue(root, "worker_view");
    ASSERT_TRUE(overlay);
    auto shared = namedValue(overlay, "shared");
    ASSERT_TRUE(shared);
    EXPECT_TRUE(shared->isReadOnly());

    auto shadows = namedValue(overlay, "shadows");
    ASSERT_TRUE(shadows);
    EXPECT_FALSE(shadows->isReadOnly());
}
