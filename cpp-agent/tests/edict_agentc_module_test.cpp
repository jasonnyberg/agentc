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

std::string runtimeLibPath() {
    return (std::filesystem::path(TEST_BUILD_DIR) / "cpp-agent" / "libagent_runtime.so").string();
}

std::string runtimeHeaderPath() {
    return (std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "include" / "agentc_runtime" / "agentc_runtime.h").string();
}

std::string extensionsLibPath() {
    return (std::filesystem::path(TEST_BUILD_DIR) / "extensions" / "libagentc_extensions.so").string();
}

std::string extensionsHeaderPath() {
    return (std::filesystem::path(TEST_SOURCE_DIR) / "extensions" / "agentc_stdlib.h").string();
}

std::string modulePath() {
    return (std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules" / "agentc.edict").string();
}

std::string projectConfigPath() {
    return (std::filesystem::path(TEST_SOURCE_DIR) / "agentc-config.json").string();
}

std::string edictBinaryPath() {
    return (std::filesystem::path(TEST_BUILD_DIR) / "edict" / "edict").string();
}

std::string runEdictScript(const std::string& script) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto scriptPath = tempDir / "agentc_module_test.ed";
    const auto outputPath = tempDir / "agentc_module_test.out";

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

bool jsonBoolish(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_string()) {
        return value.get<std::string>() == "true";
    }
    return false;
}

} // namespace

TEST(EdictAgentcModuleTest, WrapperReturnsNormalizedJsonEnvelope) {
    const std::string moduleSource = readFile(modulePath());

    std::ostringstream script;
    script << "[" << extensionsLibPath() << "] [" << extensionsHeaderPath() << "] resolver.import ! @ext\n";
    script << "[" << runtimeLibPath() << "] [" << runtimeHeaderPath() << "] resolver.import ! @runtimeffi\n";
    script << moduleSource << "\n";
    script << "[" << projectConfigPath() << "] agentc_runtime_create_path ! @rt\n";
    script << R"(rt {"foo":"bar"} agentc_call ! to_json !
print
rt agentc_destroy ! /
)";

    const std::string jsonText = runEdictScript(script.str());
    ASSERT_FALSE(jsonText.empty());

    auto parsed = nlohmann::json::parse(jsonText);
    ASSERT_TRUE(parsed.contains("ok"));
    EXPECT_FALSE(jsonBoolish(parsed["ok"]));
    ASSERT_TRUE(parsed.contains("error"));
    EXPECT_EQ(parsed["error"].value("code", std::string()), "request_invalid");
    EXPECT_EQ(parsed["error"].value("message", std::string()), "Request must provide prompt or messages");
}
