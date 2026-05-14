#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
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

std::string launcherPath() {
    return (std::filesystem::path(TEST_SOURCE_DIR) / "edict.sh").string();
}

std::string runEdictScript(const std::string& script) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto scriptPath = tempDir / "agentc_llm_module_test.ed";
    const auto outputPath = tempDir / "agentc_llm_module_test.out";

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

std::string runLauncherReplScript(const std::string& input, bool autoChat = false) {
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto inputPath = tempDir / "agentc_llm_launcher_test.in";
    const auto outputPath = tempDir / "agentc_llm_launcher_test.out";

    {
        std::ofstream out(inputPath);
        out << input;
    }

    const auto buildRoot = std::filesystem::path(TEST_BUILD_DIR);
    const auto sourceRoot = std::filesystem::path(TEST_SOURCE_DIR);
    const std::string command =
        "env EDICT_RUNTIME_LIB=\"" + (buildRoot / "cpp-agent" / "libagent_runtime_mock.so").string() +
        "\" EDICT_RUNTIME_HDR=\"" + (sourceRoot / "cpp-agent" / "include" / "agentc_runtime" / "agentc_runtime.h").string() +
        "\" EDICT_AUTO_CHAT=\"" + std::string(autoChat ? "1" : "0") +
        "\" \"" + launcherPath() + "\" < \"" + inputPath.string() + "\" > \"" + outputPath.string() + "\"";
    const int rc = std::system(command.c_str());
    EXPECT_EQ(rc, 0) << "Launcher command failed: " << command;

    const std::string output = readFile(outputPath);
    std::error_code ec;
    std::filesystem::remove(inputPath, ec);
    std::filesystem::remove(outputPath, ec);
    return output;
}

std::string bootstrapJsonForMockRuntime() {
    const auto buildRoot = std::filesystem::path(TEST_BUILD_DIR);
    const auto sourceRoot = std::filesystem::path(TEST_SOURCE_DIR);
    const nlohmann::json bootstrap = {
        {"extensions_library_path", (buildRoot / "extensions" / "libagentc_extensions.so").string()},
        {"extensions_header_path", (sourceRoot / "extensions" / "agentc_stdlib.h").string()},
        {"runtime_library_path", (buildRoot / "cpp-agent" / "libagent_runtime_mock.so").string()},
        {"runtime_header_path", (sourceRoot / "cpp-agent" / "include" / "agentc_runtime" / "agentc_runtime.h").string()}
    };
    return bootstrap.dump();
}

std::string bootstrapJsonForLiveRuntime() {
    const auto buildRoot = std::filesystem::path(TEST_BUILD_DIR);
    const auto sourceRoot = std::filesystem::path(TEST_SOURCE_DIR);
    const nlohmann::json bootstrap = {
        {"extensions_library_path", (buildRoot / "extensions" / "libagentc_extensions.so").string()},
        {"extensions_header_path", (sourceRoot / "extensions" / "agentc_stdlib.h").string()},
        {"runtime_library_path", (buildRoot / "cpp-agent" / "libagent_runtime.so").string()},
        {"runtime_header_path", (sourceRoot / "cpp-agent" / "include" / "agentc_runtime" / "agentc_runtime.h").string()}
    };
    return bootstrap.dump();
}

bool hasGoogleApiKey() {
    const char* gemini = std::getenv("GEMINI_API_KEY");
    const char* google = std::getenv("GOOGLE_API_KEY");
    return (gemini && *gemini) || (google && *google);
}

std::string trim(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), value.end());
    return value;
}

std::string lastNonEmptyLine(const std::string& output) {
    std::istringstream in(output);
    std::string line;
    std::string last;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) {
            last = line;
        }
    }
    return last;
}

} // namespace

TEST(EdictLlmModuleTest, InitBuildsProviderFromNamedPreset) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string agentcModule = readFile(base / "agentc.edict");
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string llmModule = readFile(base / "llm.edict");

    std::ostringstream script;
    script << agentcModule << "\n";
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << llmModule << "\n";
    script << bootstrapJsonForMockRuntime() << R"( llm.configure_bootstrap ! /
llm.init([local-qwen]) @provider
{"preset_name":"","runtime":null,"conversation":null,"request":""} @summary
provider.preset_name @summary.preset_name
provider.runtime @summary.runtime
provider.conversation @summary.conversation
provider.request @summary.request
summary to_json !
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_EQ(parsed["preset_name"].get<std::string>(), "local-qwen");
    EXPECT_EQ(parsed["runtime"]["default_provider"].get<std::string>(), "local");
    EXPECT_EQ(parsed["runtime"]["default_model"].get<std::string>(), "qwen");
    EXPECT_EQ(parsed["conversation"]["system_prompt"].get<std::string>(),
              "You are Qwen, created by Alibaba Cloud. You are a helpful assistant.");
    ASSERT_TRUE(parsed["request"].is_string());
}

TEST(EdictLlmModuleTest, InitBuildsOpenAICodexProviderPreset) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string agentcModule = readFile(base / "agentc.edict");
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string llmModule = readFile(base / "llm.edict");

    std::ostringstream script;
    script << agentcModule << "\n";
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << llmModule << "\n";
    script << bootstrapJsonForMockRuntime() << R"( llm.configure_bootstrap ! /
llm.init([openai-codex]) @provider
{"preset_name":"","runtime":null,"system_prompt":""} @summary
provider.preset_name @summary.preset_name
provider.runtime @summary.runtime
provider.conversation.system_prompt @summary.system_prompt
summary to_json !
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_EQ(parsed["preset_name"].get<std::string>(), "openai-codex");
    EXPECT_EQ(parsed["runtime"]["default_provider"].get<std::string>(), "openai-codex");
    EXPECT_EQ(parsed["runtime"]["default_model"].get<std::string>(), "gpt-5.3-codex");
    EXPECT_EQ(parsed["runtime"]["provider_contract"]["transport_api"].get<std::string>(), "openai-codex-responses");
    EXPECT_EQ(parsed["system_prompt"].get<std::string>(), "You are a helpful coding assistant.");
}

TEST(EdictLlmModuleTest, InitBuildsGoogleGemmaCheapPreset) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string agentcModule = readFile(base / "agentc.edict");
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string llmModule = readFile(base / "llm.edict");

    std::ostringstream script;
    script << agentcModule << "\n";
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << llmModule << "\n";
    script << bootstrapJsonForMockRuntime() << R"( llm.configure_bootstrap ! /
llm.init([gemma-4-31b-it]) @provider
{"preset_name":"","runtime":null} @summary
provider.preset_name @summary.preset_name
provider.runtime @summary.runtime
summary to_json !
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_EQ(parsed["preset_name"].get<std::string>(), "gemma-4-31b-it");
    EXPECT_EQ(parsed["runtime"]["default_provider"].get<std::string>(), "google");
    EXPECT_EQ(parsed["runtime"]["default_model"].get<std::string>(), "gemma-4-31b-it");
}

TEST(EdictLlmModuleTest, LiveGoogleGemmaRequestReturnsOkWhenCredentialsArePresent) {
    if (!hasGoogleApiKey()) {
        GTEST_SKIP() << "Set GEMINI_API_KEY or GOOGLE_API_KEY to run live Google/Gemma LLM smoke coverage";
    }

    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string agentcModule = readFile(base / "agentc.edict");
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string llmModule = readFile(base / "llm.edict");

    std::ostringstream script;
    script << agentcModule << "\n";
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << llmModule << "\n";
    script << bootstrapJsonForLiveRuntime() << R"( llm.configure_bootstrap ! /
llm.init([gemma-4-31b-it]) @provider
provider < [Reply with exactly two lowercase letters: ok] request ! > pop /
provider.assistant_text print
)";

    const std::string output = runEdictScript(script.str());
    EXPECT_EQ(lastNonEmptyLine(output), "ok") << output;
}

TEST(EdictLlmModuleTest, ProviderStreamStartAndSyncUsesRuntimeStreamApi) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string agentcModule = readFile(base / "agentc.edict");
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string llmModule = readFile(base / "llm.edict");

    std::ostringstream script;
    script << agentcModule << "\n";
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << llmModule << "\n";
    script << bootstrapJsonForMockRuntime() << R"( llm.configure_bootstrap ! /
llm.init([local-qwen]) @provider
provider < [stream prompt] stream_start ! > pop /
provider < stream_sync ! > pop /
{"stream_id":"","assistant_text":"","stream_last":null,"stream_complete":[]} @summary
provider.stream_id @summary.stream_id
provider.assistant_text @summary.assistant_text
provider.stream_last @summary.stream_last
provider.stream_complete @summary.stream_complete
summary to_json !
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_NE(parsed["stream_id"].get<std::string>().find("mock_stream_"), std::string::npos);
    EXPECT_EQ(parsed["assistant_text"].get<std::string>(), "mock-stream:stream prompt");
    EXPECT_EQ(parsed["stream_last"]["tokens"].get<std::string>(), "mock-stream:stream prompt");
    EXPECT_FALSE(parsed["stream_complete"].empty());
}

TEST(EdictLlmModuleTest, ProviderRequestRunsTurnAndReturnsUpdatedProvider) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string agentcModule = readFile(base / "agentc.edict");
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string llmModule = readFile(base / "llm.edict");

    std::ostringstream script;
    script << agentcModule << "\n";
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << llmModule << "\n";
    script << bootstrapJsonForMockRuntime() << R"( llm.configure_bootstrap ! /
llm.init([local-qwen]) @provider
provider < [What is the capital of France?] request ! > pop /
{"assistant_text":"","last_response":null,"messages":[]} @summary
provider.assistant_text @summary.assistant_text
provider.conversation.last_response @summary.last_response
provider.conversation.messages @summary.messages
summary to_json !
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_EQ(parsed["assistant_text"].get<std::string>(), "mock:What is the capital of France?");
    EXPECT_EQ(parsed["last_response"]["ok"].get<std::string>(), "true");
    EXPECT_EQ(parsed["last_response"]["provider"].get<std::string>(), "local");
    EXPECT_EQ(parsed["last_response"]["model"].get<std::string>(), "qwen");
    ASSERT_TRUE(parsed["messages"].is_array());
    ASSERT_EQ(parsed["messages"].size(), 2u);
    EXPECT_EQ(parsed["messages"][0]["content"].get<std::string>(),
               "What is the capital of France?");
    EXPECT_EQ(parsed["messages"][1]["text"].get<std::string>(),
               "mock:What is the capital of France?");
}

TEST(EdictLlmModuleTest, ProviderRequestSupportsRepeatedTurns) {
    const auto base = std::filesystem::path(TEST_SOURCE_DIR) / "cpp-agent" / "edict" / "modules";
    const std::string agentcModule = readFile(base / "agentc.edict");
    const std::string statefulModule = readFile(base / "agentc_stateful_loop.edict");
    const std::string providerModule = readFile(base / "agentc_provider_contracts.edict");
    const std::string llmModule = readFile(base / "llm.edict");

    std::ostringstream script;
    script << agentcModule << "\n";
    script << statefulModule << "\n";
    script << providerModule << "\n";
    script << llmModule << "\n";
    script << bootstrapJsonForMockRuntime() << R"( llm.configure_bootstrap ! /
llm.init([local-qwen]) @provider
provider < [first] request ! > pop /
provider < [second] request ! > pop /
{"assistant_text":"","last_response":null,"messages":[]} @summary
provider.assistant_text @summary.assistant_text
provider.conversation.last_response @summary.last_response
provider.conversation.messages @summary.messages
summary to_json !
print
)";

    const std::string output = runEdictScript(script.str());
    auto parsed = nlohmann::json::parse(output);
    EXPECT_EQ(parsed["assistant_text"].get<std::string>(), "mock:second");
    EXPECT_EQ(parsed["last_response"]["message"]["text"].get<std::string>(), "mock:second");
    ASSERT_TRUE(parsed["messages"].is_array());
    ASSERT_EQ(parsed["messages"].size(), 4u);
    EXPECT_EQ(parsed["messages"][0]["content"].get<std::string>(), "first");
    EXPECT_EQ(parsed["messages"][1]["text"].get<std::string>(), "mock:first");
    EXPECT_EQ(parsed["messages"][2]["content"].get<std::string>(), "second");
    EXPECT_EQ(parsed["messages"][3]["text"].get<std::string>(), "mock:second");
}

TEST(EdictLlmModuleTest, ProviderReplHandlesMultiplePromptsAndEof) {
    const std::string output = runLauncherReplScript(
        "llm.init([local-qwen]) @provider\n"
        "provider < repl ! > pop /\n"
        "Hello from repl\n"
        "\n"
        "Second prompt\n",
        false);

    EXPECT_NE(output.find("Chat ready. Press Ctrl-D to exit."), std::string::npos);
    EXPECT_NE(output.find("mock:Hello from repl"), std::string::npos);
    EXPECT_NE(output.find("mock:Second prompt"), std::string::npos);
}

TEST(EdictLlmModuleTest, LauncherAutoChatStartsDefaultProviderRepl) {
    const std::string output = runLauncherReplScript(
        "Hello from auto chat\n"
        "\n"
        "Second auto prompt\n",
        true);

    EXPECT_NE(output.find("Chat ready. Press Ctrl-D to exit."), std::string::npos);
    EXPECT_NE(output.find("mock:Hello from auto chat"), std::string::npos);
    EXPECT_NE(output.find("mock:Second auto prompt"), std::string::npos);
}
