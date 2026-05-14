#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << "Failed to open file: " << path;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string edictBinaryPath() {
    return (std::filesystem::path(TEST_BUILD_DIR) / "edict" / "edict").string();
}

std::string runEdictScript(const std::string& script) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto scriptPath = tempDir / "agentc_agent_root_test.ed";
    const auto outputPath = tempDir / "agentc_agent_root_test.out";

    {
        std::ofstream out(scriptPath);
        out << script;
    }

    const std::string command = edictBinaryPath() + " - < " + scriptPath.string() + " > " + outputPath.string();
    const int rc = std::system(command.c_str());
    EXPECT_EQ(rc, 0) << "Edict command failed: " << command;

    const std::string output = readFile(outputPath);
    std::error_code ec;
    std::filesystem::remove(scriptPath, ec);
    std::filesystem::remove(outputPath, ec);
    return output;
}

} // namespace

TEST(EdictAgentRootTest, BuildsCanonicalRootShape) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string rootModule = readFile(base / "agentc_agent_root.edict");

    std::ostringstream script;
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << rootModule << "\n";
    script << R"([system prompt] agentc_agent_root_init! @root
root to_json!
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    ASSERT_TRUE(parsed["conversation"].is_object());
    ASSERT_TRUE(parsed["memory"].is_object());
    ASSERT_TRUE(parsed["policy"].is_object());
    ASSERT_TRUE(parsed["runtime"].is_object());
    ASSERT_TRUE(parsed["loop"].is_object());
    EXPECT_EQ(parsed["conversation"]["system_prompt"].get<std::string>(), "system prompt");
    EXPECT_EQ(parsed["runtime"]["default_provider"].get<std::string>(), "google");
    EXPECT_EQ(parsed["loop"]["status"].get<std::string>(), "ready");
}

TEST(EdictAgentRootTest, ProviderCatalogCarriesContrastingContracts) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");

    std::ostringstream script;
    script << providerModule << "\n";
    script << R"(agentc_provider_catalog! to_json!
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_EQ(parsed["local"]["family"].get<std::string>(), "openai-compatible");
    EXPECT_EQ(parsed["local"]["default_model"].get<std::string>(), "qwen");
    EXPECT_EQ(parsed["google"]["family"].get<std::string>(), "gemini-generate-content");
    EXPECT_EQ(parsed["google"]["default_model"].get<std::string>(), "gemini-3.1-pro-preview");
}

TEST(EdictAgentRootTest, RootBuildsCanonicalTurnRequestFromProviderContract) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string rootModule = readFile(base / "agentc_agent_root.edict");

    std::ostringstream script;
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << rootModule << "\n";
    script << R"(agentc_provider_local_runtime_config! @runtime_config
[system prompt] runtime_config agentc_agent_root_init_with_runtime! @root
root [What is the capital of France?] agentc_agent_root_stage_user_turn! @root
root agentc_agent_root_build_turn_request! to_json!
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_EQ(parsed["provider"].get<std::string>(), "local");
    EXPECT_EQ(parsed["model"].get<std::string>(), "qwen");
    EXPECT_EQ(parsed["system"].get<std::string>(), "system prompt");
    EXPECT_EQ(parsed["response_mode"].get<std::string>(), "text");
    EXPECT_EQ(parsed["metadata"]["provider_contract"].get<std::string>(), "local");
    EXPECT_EQ(parsed["metadata"]["provider_family"].get<std::string>(), "openai-compatible");
    ASSERT_TRUE(parsed["messages"].is_array());
    ASSERT_EQ(parsed["messages"].size(), 1u);
}
