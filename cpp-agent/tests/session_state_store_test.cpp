#include <gtest/gtest.h>

#include "../runtime/persistence/agent_root_state.h"
#include "../runtime/persistence/session_state_store.h"

#include <filesystem>
#include <nlohmann/json.hpp>

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
