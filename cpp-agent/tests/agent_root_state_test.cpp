#include <gtest/gtest.h>

#include "../runtime/persistence/agent_root_state.h"

#include <nlohmann/json.hpp>

TEST(AgentRootStateTest, NormalizesLegacyTranscriptIntoCanonicalRoot) {
    const nlohmann::json legacy = {
        {"system_prompt", "legacy prompt"},
        {"messages", nlohmann::json::array({
            nlohmann::json{{"role", "user"}, {"text", "hello"}},
            nlohmann::json{{"role", "assistant"}, {"text", "world"}}
        })}
    };

    const auto root = agentc::runtime::normalize_agent_root(legacy, "fallback prompt", "google", "gemini-2.5-flash");
    ASSERT_TRUE(root["conversation"].is_object());
    ASSERT_TRUE(root["memory"].is_object());
    ASSERT_TRUE(root["policy"].is_object());
    ASSERT_TRUE(root["runtime"].is_object());
    ASSERT_TRUE(root["loop"].is_object());
    EXPECT_EQ(root["conversation"]["system_prompt"].get<std::string>(), "legacy prompt");
    ASSERT_EQ(root["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(root["runtime"]["default_provider"].get<std::string>(), "google");
    EXPECT_EQ(root["runtime"]["default_model"].get<std::string>(), "gemini-2.5-flash");
}

TEST(AgentRootStateTest, AppliesRuntimeResponseIntoCanonicalRoot) {
    auto root = agentc::runtime::make_default_agent_root("system", "google", "gemini-2.5-flash");
    const nlohmann::json response = {
        {"ok", true},
        {"message", {
            {"role", "assistant"},
            {"text", "Hello world!"}
        }}
    };

    agentc::runtime::apply_runtime_response_to_agent_root(root, "Say hello", response);

    EXPECT_EQ(root["conversation"]["last_prompt"].get<std::string>(), "Say hello");
    EXPECT_EQ(root["conversation"]["assistant_text"].get<std::string>(), "Hello world!");
    ASSERT_EQ(root["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(root["conversation"]["messages"][0]["role"].get<std::string>(), "user");
    EXPECT_EQ(root["conversation"]["messages"][1]["role"].get<std::string>(), "assistant");
    EXPECT_EQ(root["loop"]["status"].get<std::string>(), "turn-complete");
    EXPECT_TRUE(root["conversation"]["last_response"]["ok"].get<bool>());
}
