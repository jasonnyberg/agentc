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

std::string modulePath() {
    return (std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules" / "agentc_stateful_loop.edict").string();
}

std::string runEdictScript(const std::string& script) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto scriptPath = tempDir / "agentc_stateful_loop_test.ed";
    const auto outputPath = tempDir / "agentc_stateful_loop_test.out";

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

TEST(EdictStatefulLoopTest, BuildsStructuredTurnState) {
    const std::string moduleSource = readFile(modulePath());

    std::ostringstream script;
    script << moduleSource << "\n";
    script << R"([system prompt] agentc_state_init ! @state
{} @response
'hello @response.message.text
[demo prompt] @state.last_prompt
response @state.last_response
response.message.text @state.assistant_text
state to_json !
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_EQ(parsed["system_prompt"].get<std::string>(), "system prompt");
    EXPECT_EQ(parsed["last_prompt"].get<std::string>(), "demo prompt");
    EXPECT_EQ(parsed["assistant_text"].get<std::string>(), "hello");
    ASSERT_TRUE(parsed["last_response"].is_object());
    EXPECT_EQ(parsed["last_response"]["message"]["text"].get<std::string>(), "hello");
}
