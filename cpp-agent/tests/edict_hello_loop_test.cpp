#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

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
    return (std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules" / "agentc_hello_loop.edict").string();
}

std::string runEdictScript(const std::string& script) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto scriptPath = tempDir / "agentc_hello_loop_test.ed";
    const auto outputPath = tempDir / "agentc_hello_loop_test.out";

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

TEST(EdictHelloLoopTest, ExtractsAssistantTextFromNormalizedResponseShape) {
    const std::string moduleSource = readFile(modulePath());

    std::ostringstream script;
    script << moduleSource << "\n";
    script << R"({"message":{"text":"hello from loop"}} agentc_extract_text !
print
)";

    const std::string output = runEdictScript(script.str());
    EXPECT_NE(output.find("hello from loop"), std::string::npos);
}
