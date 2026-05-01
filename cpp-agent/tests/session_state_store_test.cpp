#include <gtest/gtest.h>

#include "../runtime/persistence/session_state_store.h"

#include <filesystem>
#include <nlohmann/json.hpp>

TEST(SessionStateStoreTest, RoundTripsSessionJsonThroughListreeBackedStore) {
    const auto base = (std::filesystem::temp_directory_path() / "agentc_session_state_test").string();
    agentc::runtime::SessionStateStore store(base);
    store.clear();

    nlohmann::json session = {
        {"system_prompt", "persist me"},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"role", "user"}, {"text", "hello"}},
            nlohmann::json{{"role", "assistant"}, {"text", "world"}}
        })}
    };

    std::string error;
    ASSERT_TRUE(store.save(session, &error)) << error;
    ASSERT_TRUE(store.exists());

    nlohmann::json restored;
    ASSERT_TRUE(store.load(restored, &error)) << error;
    ASSERT_EQ(restored["system_prompt"].get<std::string>(), "persist me");
    ASSERT_EQ(restored["messages"].size(), 2u);
    EXPECT_EQ(restored["messages"][0]["role"].get<std::string>(), "user");
    EXPECT_EQ(restored["messages"][1]["text"].get<std::string>(), "world");

    store.clear();
    EXPECT_FALSE(store.exists());
}
