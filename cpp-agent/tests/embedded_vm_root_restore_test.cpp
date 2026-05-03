#include <gtest/gtest.h>

#include "../runtime/persistence/agent_root_vm_ops.h"
#include "../runtime/persistence/session_state_store.h"
#include "../../edict/edict_vm.h"

#include <filesystem>
#include <nlohmann/json.hpp>

TEST(EmbeddedVmRootRestoreTest, ConstructsVmFromRestoredAnchoredRoot) {
    const auto base = (std::filesystem::temp_directory_path() / "agentc_vm_root_restore_test").string();
    agentc::runtime::SessionStateStore store(base);
    store.clear();

    auto rootJson = agentc::runtime::make_default_agent_root("persisted prompt", "google", "gemini-2.5-flash");
    rootJson["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "hello"}},
        nlohmann::json{{"role", "assistant"}, {"text", "world"}}
    });
    rootJson["loop"]["status"] = "turn-complete";

    auto rootValue = agentc::fromJson(rootJson.dump());
    ASSERT_TRUE(rootValue);

    std::string error;
    ASSERT_TRUE(store.saveRoot(rootValue, &error)) << error;

    CPtr<agentc::ListreeValue> restoredRoot;
    ASSERT_TRUE(store.loadRoot(restoredRoot, &error)) << error;
    ASSERT_TRUE(restoredRoot);

    agentc::edict::EdictVM vm(restoredRoot);
    const auto restoredJson = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()));

    ASSERT_TRUE(restoredJson["conversation"].is_object());
    EXPECT_EQ(restoredJson["conversation"]["system_prompt"].get<std::string>(), "persisted prompt");
    ASSERT_EQ(restoredJson["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(restoredJson["conversation"]["messages"][1]["text"].get<std::string>(), "world");
    EXPECT_EQ(restoredJson["loop"]["status"].get<std::string>(), "turn-complete");

    store.clear();
}
