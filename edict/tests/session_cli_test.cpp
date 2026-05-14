#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>

#ifndef TEST_EDICT_BIN_DIR
#define TEST_EDICT_BIN_DIR "."
#endif

namespace {

std::string shellQuote(const std::string& value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

struct CommandResult {
    int exitCode = -1;
    std::string output;
};

CommandResult runCommand(const std::string& command) {
    CommandResult result;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe) {
        result.output = "popen failed";
        return result;
    }

    std::array<char, 512> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output += buffer.data();
    }
    const int status = pclose(pipe);
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else {
        result.exitCode = status;
    }
    return result;
}

std::filesystem::path edictPath() {
    return std::filesystem::path(TEST_EDICT_BIN_DIR) / "edict";
}

} // namespace

TEST(EdictSessionCliTest, SessionFlagPersistsAndRestoresRootBindings) {
    const auto base = std::filesystem::temp_directory_path() / "agentc_edict_session_cli_test";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    ASSERT_FALSE(ec);

    const std::string edict = shellQuote(edictPath().string());
    const std::string baseArg = shellQuote(base.string());
    const std::string session = "alpha.session";

    auto first = runCommand(edict + " --session " + shellQuote(session) +
                            " --session-base " + baseArg +
                            " -e " + shellQuote("'persisted @answer"));
    ASSERT_EQ(first.exitCode, 0) << first.output;

    EXPECT_TRUE(std::filesystem::exists(base / session / "manifest.json"));
    EXPECT_TRUE(std::filesystem::exists(base / session / "roots.bin"));

    auto second = runCommand(edict + " --session " + shellQuote(session) +
                             " --session-base " + baseArg +
                             " -e " + shellQuote("answer print"));
    ASSERT_EQ(second.exitCode, 0) << second.output;
    EXPECT_NE(second.output.find("persisted"), std::string::npos) << second.output;

    std::filesystem::remove_all(base, ec);
}

TEST(EdictSessionCliTest, RejectsUnsafeSessionIds) {
    const auto base = std::filesystem::temp_directory_path() / "agentc_edict_session_cli_bad_id_test";
    std::error_code ec;
    std::filesystem::remove_all(base, ec);
    std::filesystem::create_directories(base, ec);
    ASSERT_FALSE(ec);

    const std::string edict = shellQuote(edictPath().string());
    auto result = runCommand(edict + " --session " + shellQuote("../escape") +
                             " --session-base " + shellQuote(base.string()) +
                             " -e " + shellQuote("'x"));
    EXPECT_NE(result.exitCode, 0) << result.output;
    EXPECT_NE(result.output.find("Invalid session id"), std::string::npos) << result.output;
    EXPECT_FALSE(std::filesystem::exists(base / ".." / "escape"));

    std::filesystem::remove_all(base, ec);
}
