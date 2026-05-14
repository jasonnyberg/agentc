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

std::string mockRuntimeLibPath() {
    return (std::filesystem::path(TEST_BUILD_DIR) / "cpp-agent" / "libagent_runtime_mock.so").string();
}

std::string launcherPath() {
    return (std::filesystem::path(TEST_SOURCE_DIR) / "edict.sh").string();
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

std::string runLauncherScript(const std::string& script) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto scriptPath = tempDir / "agentc_module_launcher_test.ed";
    const auto outputPath = tempDir / "agentc_module_launcher_test.out";

    {
        std::ofstream out(scriptPath);
        out << script;
    }

    const std::string command =
        "env EDICT_RUNTIME_LIB=\"" + mockRuntimeLibPath() +
        "\" EDICT_RUNTIME_HDR=\"" + runtimeHeaderPath() +
        "\" EDICT_AUTO_CHAT=0 \"" + launcherPath() +
        "\" - < \"" + scriptPath.string() + "\" > \"" + outputPath.string() + "\"";
    const int rc = std::system(command.c_str());
    EXPECT_EQ(rc, 0) << "Launcher command failed: " << command;

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
    script << "[" << extensionsLibPath() << "] [" << extensionsHeaderPath() << "] resolver.import! @ext\n";
    script << "[" << runtimeLibPath() << "] [" << runtimeHeaderPath() << "] resolver.import! @runtimeffi\n";
    script << moduleSource << "\n";
    script << "[" << projectConfigPath() << "] agentc_runtime_create_path! @rt\n";
    script << R"(rt {"foo":"bar"} agentc_call! to_json!
print
rt agentc_destroy! /
)";

    const std::string jsonText = runEdictScript(script.str());
    ASSERT_FALSE(jsonText.empty());
    
    // Add debugging print
    std::cout << "Edict returned string: '" << jsonText << "'" << std::endl;

    auto parsed = nlohmann::json::parse(jsonText);
    ASSERT_TRUE(parsed.contains("ok"));
    EXPECT_FALSE(jsonBoolish(parsed["ok"]));
    ASSERT_TRUE(parsed.contains("error"));
    EXPECT_EQ(parsed["error"].value("code", std::string()), "request_invalid");
    EXPECT_EQ(parsed["error"].value("message", std::string()), "Request must provide prompt or messages");
}

TEST(EdictAgentcModuleTest, ToolWrappersReadWriteReplaceAndShell) {
    const std::string moduleSource = readFile(modulePath());
    const auto dir = std::filesystem::temp_directory_path() / "agentc_g079_tool_wrappers";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    const auto target = dir / "note.txt";

    std::ostringstream script;
    script << "[" << extensionsLibPath() << "] [" << extensionsHeaderPath() << "] resolver.import! @ext\n";
    script << "[" << runtimeLibPath() << "] [" << runtimeHeaderPath() << "] resolver.import! @runtimeffi\n";
    script << moduleSource << "\n";
    script << "[" << target.string() << "] [alpha old omega] agentc_file_write! @write_result\n";
    script << "[" << target.string() << "] [old] [new] agentc_file_replace! @replace_result\n";
    script << "[" << target.string() << "] agentc_file_read! @read_result\n";
    script << "[printf shell-ok] agentc_shell! @shell_result\n";
    script << R"({"write":null,"replace":null,"read":null,"shell":null} @summary
write_result @summary.write
replace_result @summary.replace
read_result @summary.read
shell_result @summary.shell
summary to_json! print
)";

    const std::string jsonText = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(jsonText);
    EXPECT_FALSE(parsed["write"]["ok"].empty());
    EXPECT_FALSE(parsed["replace"]["ok"].empty());
    EXPECT_FALSE(parsed["read"]["ok"].empty());
    EXPECT_EQ(parsed["read"]["content"].get<std::string>(), "alpha new omega");
    EXPECT_FALSE(parsed["shell"]["ok"].empty());
    EXPECT_EQ(parsed["shell"]["exit_code"].get<std::string>(), "0");
    EXPECT_EQ(parsed["shell"]["output"].get<std::string>(), "shell-ok");

    std::filesystem::remove_all(dir, ec);
}

TEST(EdictAgentcModuleTest, ProviderCarriesToolSurfaceUnderCuratedLauncher) {
    const auto dir = std::filesystem::temp_directory_path() / "agentc_g079_provider_tools";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    const auto target = dir / "provider-note.txt";

    std::ostringstream script;
    script << "llm.init([local-qwen]) @provider\n";
    script << "provider < [" << target.string() << "] [provider tool text] tools.write_file! @tool_write_result > / /\n";
    script << "provider < [" << target.string() << "] tools.read_file! @tool_read_result > / /\n";
    script << R"({"write":null,"read":null} @summary
provider.tool_write_result @summary.write
provider.tool_read_result @summary.read
summary to_json! print
)";

    const std::string jsonText = runLauncherScript(script.str());
    auto parsed = nlohmann::json::parse(jsonText);
    EXPECT_FALSE(parsed["write"]["ok"].empty());
    EXPECT_FALSE(parsed["read"]["ok"].empty());
    EXPECT_EQ(parsed["read"]["content"].get<std::string>(), "provider tool text");

    std::filesystem::remove_all(dir, ec);
}

TEST(EdictAgentcModuleTest, StreamWrapperSpawnsAndSynchronizes) {
    const std::string moduleSource = readFile(modulePath());

    std::ostringstream script;
    script << "[" << extensionsLibPath() << "] [" << extensionsHeaderPath() << "] resolver.import! @ext\n";
    script << "[" << runtimeLibPath() << "] [" << runtimeHeaderPath() << "] resolver.import! @runtimeffi\n";
    script << moduleSource << "\n";
    script << "[" << projectConfigPath() << "] agentc_runtime_create_path! @rt\n";
    
    script << R"(
    rt {"prompt": "hello", "stream_test_text": "hello-stream"} agentc_call_stream! @sid
    rt sid agentc_stream_sync! @sync_result
    sync_result to_json! print
    rt agentc_destroy! /
    )";

    const std::string jsonText = runEdictScript(script.str());
    ASSERT_FALSE(jsonText.empty());
    
    auto parsed = nlohmann::json::parse(jsonText);
    ASSERT_TRUE(parsed.contains("complete"));
    ASSERT_FALSE(parsed["complete"].empty());
    ASSERT_TRUE(parsed.contains("tokens"));
    EXPECT_EQ(parsed["tokens"].get<std::string>(), "hello-stream");
}
