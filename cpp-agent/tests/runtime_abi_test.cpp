#include <gtest/gtest.h>

#include <agentc_runtime/agentc_runtime.h>
#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;
}

TEST(AgentRuntimeAbiTest, CreateRequestAndDestroyReturnsNormalizedErrorEnvelope) {
    agentc_runtime_t runtime = agentc_runtime_create_json(R"({"default_provider":"google","default_model":"gemini-2.5-flash"})");
    ASSERT_NE(runtime, nullptr);

    char* response = agentc_runtime_request_json(runtime, R"({"foo":"bar"})");
    ASSERT_NE(response, nullptr);

    json parsed = json::parse(response);
    EXPECT_FALSE(parsed.value("ok", true));
    ASSERT_TRUE(parsed.contains("error"));
    EXPECT_EQ(parsed["error"].value("code", std::string()), "request_invalid");
    EXPECT_EQ(parsed["finish_reason"].get<std::string>(), "error");

    agentc_runtime_free_string(response);
    agentc_runtime_destroy(runtime);
}

TEST(AgentRuntimeAbiTest, InvalidConfigureReportsStructuredLastError) {
    agentc_runtime_t runtime = agentc_runtime_create_json("{}");
    ASSERT_NE(runtime, nullptr);

    EXPECT_NE(agentc_runtime_configure_json(runtime, "[]"), 0);

    char* error_json = agentc_runtime_last_error_json(runtime);
    ASSERT_NE(error_json, nullptr);
    json error = json::parse(error_json);
    EXPECT_EQ(error.value("code", std::string()), "config_invalid");
    EXPECT_FALSE(error.value("retryable", true));

    agentc_runtime_free_string(error_json);
    agentc_runtime_destroy(runtime);
}
