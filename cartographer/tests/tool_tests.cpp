// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// AgentC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with AgentC. If not, see <https://www.gnu.org/licenses/>.

#include <gtest/gtest.h>

#include "../parser.h"
#include "../protocol.h"
#include "../resolver.h"
#include "../tool_cli.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace agentc::cartographer;

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

namespace {

struct ProcessResult {
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
};

static bool readAllFromFd(int fd, std::string& out) {
    out.clear();
    char buffer[4096];
    while (true) {
        ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count == 0) {
            return true;
        }
        if (count < 0) {
            return false;
        }
        out.append(buffer, static_cast<size_t>(count));
    }
}

static CPtr<agentc::ListreeValue> makeArgList(std::initializer_list<std::string> values) {
    CPtr<agentc::ListreeValue> args = agentc::createListValue();
    for (const std::string& value : values) {
        args->put(agentc::createStringValue(value), false);
    }
    return args;
}

static size_t argCount(CPtr<agentc::ListreeValue> args) {
    size_t count = 0;
    if (!args) {
        return 0;
    }
    args->forEachList([&count](CPtr<agentc::ListreeValueRef>&) {
        ++count;
    });
    return count;
}

static std::string argAt(CPtr<agentc::ListreeValue> args, size_t index) {
    std::string result;
    if (!args) {
        return result;
    }

    size_t current = 0;
    args->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (current != index) {
            ++current;
            return;
        }

        CPtr<agentc::ListreeValue> value = ref->getValue();
        if (value && value->getData() && value->getLength() > 0) {
            result.assign(static_cast<const char*>(value->getData()), static_cast<size_t>(value->getLength()));
        }
        current = index + 1;
    });
    return result;
}

static ProcessResult runProcess(CPtr<agentc::ListreeValue> args,
                                const std::string& stdinText = std::string()) {
    ProcessResult result;
    const size_t argc = argCount(args);
    if (argc == 0) {
        result.stderrText = "No command provided";
        return result;
    }

    int stdinPipe[2] = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    if (::pipe(stdinPipe) != 0 || ::pipe(stdoutPipe) != 0 || ::pipe(stderrPipe) != 0) {
        result.stderrText = "Failed to create process pipes";
        return result;
    }

    pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(stdinPipe[0], STDIN_FILENO);
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        ::dup2(stderrPipe[1], STDERR_FILENO);

        ::close(stdinPipe[0]);
        ::close(stdinPipe[1]);
        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        ::close(stderrPipe[0]);
        ::close(stderrPipe[1]);

        char** argv = new char*[argc + 1];
        for (size_t i = 0; i < argc; ++i) {
            std::string arg = argAt(args, i);
            argv[i] = new char[arg.size() + 1];
            std::memcpy(argv[i], arg.c_str(), arg.size() + 1);
        }
        argv[argc] = nullptr;
        ::execv(argv[0], argv);
        _exit(127);
    }

    ::close(stdinPipe[0]);
    ::close(stdoutPipe[1]);
    ::close(stderrPipe[1]);

    if (!stdinText.empty()) {
        const char* bytes = stdinText.data();
        size_t remaining = stdinText.size();
        while (remaining > 0) {
            ssize_t written = ::write(stdinPipe[1], bytes, remaining);
            if (written <= 0) {
                break;
            }
            bytes += written;
            remaining -= static_cast<size_t>(written);
        }
    }
    ::close(stdinPipe[1]);

    readAllFromFd(stdoutPipe[0], result.stdoutText);
    readAllFromFd(stderrPipe[0], result.stderrText);
    ::close(stdoutPipe[0]);
    ::close(stderrPipe[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) == pid) {
        if (WIFEXITED(status)) {
            result.exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exitCode = 128 + WTERMSIG(status);
        }
    }
    return result;
}

} // namespace

TEST(CartographerToolTest, StandaloneParserProducesParserJsonSchema) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();

    std::string json;
    std::string error;
    ASSERT_TRUE(parser::parseHeaderToParserJson(headerPath, json, error)) << error;
    EXPECT_NE(json.find("\"symbols\""), std::string::npos);
    EXPECT_NE(json.find("\"add\""), std::string::npos);

    Mapper::ParseDescription description;
    ASSERT_TRUE(protocol::decodeParseDescription(json, description, error)) << error;
    ASSERT_TRUE(description.hasSymbols());
    CPtr<Mapper::NodeDescription> firstDescription = description.firstSymbol();
    ASSERT_TRUE(firstDescription);
    EXPECT_EQ(firstDescription->key, "add");
}

TEST(CartographerToolTest, ParseCommandLibrarySupportsStandaloneToolFlow) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();

    std::ostringstream output;
    std::ostringstream error;
    const int exitCode = toolcli::runParseCommand(makeArgList({headerPath}), output, error);
    EXPECT_EQ(exitCode, 0) << error.str();
    EXPECT_TRUE(error.str().empty());
    EXPECT_NE(output.str().find("\"symbols\""), std::string::npos);
}

TEST(CartographerToolTest, StandaloneResolverAnnotatesLibraryFingerprintAndSymbols) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string libraryPath = (buildDir / "libagentmath_poc.so").string();

    Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(parser::parseHeaderToDescription(headerPath, description, error)) << error;

    resolver::ResolvedApi resolved;
    ASSERT_TRUE(resolver::resolveApiDescription(libraryPath, description, resolved, error)) << error;
    EXPECT_EQ(resolved.libraryPath, libraryPath);
    EXPECT_FALSE(resolved.contentHash.empty());
    EXPECT_GT(resolved.fileSize, 0u);
    EXPECT_GT(resolved.symbolCount, 0u);
    ASSERT_TRUE(resolved.hasSymbols());
    CPtr<resolver::ResolvedSymbol> firstResolved = resolved.firstSymbol();
    ASSERT_TRUE(firstResolved);
    EXPECT_EQ(firstResolved->key, "add");
    EXPECT_EQ(firstResolved->resolutionStatus, "resolved");
    EXPECT_FALSE(firstResolved->address.empty());

    const std::string json = resolver::encodeResolvedApi(resolved);
    EXPECT_NE(json.find("\"format\":\"resolver_json_v1\""), std::string::npos);
    EXPECT_NE(json.find("\"content_hash\":"), std::string::npos);
    EXPECT_NE(json.find("\"resolution_status\":\"resolved\""), std::string::npos);
}

TEST(CartographerToolTest, ResolveCommandLibraryConsumesSchemaFromStdin) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string libraryPath = (buildDir / "libagentmath_poc.so").string();

    std::string schemaJson;
    std::string parseError;
    ASSERT_TRUE(parser::parseHeaderToParserJson(headerPath, schemaJson, parseError)) << parseError;

    std::istringstream input(schemaJson);
    std::ostringstream output;
    std::ostringstream error;
    const int exitCode = toolcli::runResolveCommand(makeArgList({libraryPath, "-"}), input, output, error);
    EXPECT_EQ(exitCode, 0) << error.str();
    EXPECT_TRUE(error.str().empty());
    EXPECT_NE(output.str().find("\"format\":\"resolver_json_v1\""), std::string::npos);
    EXPECT_NE(output.str().find("\"resolution_status\":\"resolved\""), std::string::npos);
}

TEST(CartographerToolTest, StandaloneParserProcessProducesParserJsonSchema) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string exePath = (buildDir / "cartographer_parse").string();

    ProcessResult result = runProcess(makeArgList({exePath, headerPath}));
    ASSERT_EQ(result.exitCode, 0) << result.stderrText;
    EXPECT_TRUE(result.stderrText.empty());

    Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(protocol::decodeParseDescription(result.stdoutText, description, error)) << error;
    ASSERT_TRUE(description.hasSymbols());
    CPtr<Mapper::NodeDescription> firstDescription = description.firstSymbol();
    ASSERT_TRUE(firstDescription);
    EXPECT_EQ(firstDescription->key, "add");
}

TEST(CartographerToolTest, StandaloneResolverProcessConsumesPipelineSchemaAndResolvesSymbols) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string libraryPath = (buildDir / "libagentmath_poc.so").string();
    const std::string parseExe = (buildDir / "cartographer_parse").string();
    const std::string resolveExe = (buildDir / "cartographer_resolve").string();

    ProcessResult parseResult = runProcess(makeArgList({parseExe, headerPath}));
    ASSERT_EQ(parseResult.exitCode, 0) << parseResult.stderrText;

    ProcessResult resolveResult = runProcess(makeArgList({resolveExe, libraryPath, "-"}), parseResult.stdoutText);
    ASSERT_EQ(resolveResult.exitCode, 0) << resolveResult.stderrText;
    EXPECT_TRUE(resolveResult.stderrText.empty());

    resolver::ResolvedApi resolved;
    std::string error;
    ASSERT_TRUE(resolver::decodeResolvedApi(resolveResult.stdoutText, resolved, error)) << error;
    EXPECT_EQ(resolved.libraryPath, libraryPath);
    ASSERT_TRUE(resolved.hasSymbols());
    CPtr<resolver::ResolvedSymbol> firstResolved = resolved.firstSymbol();
    ASSERT_TRUE(firstResolved);
    EXPECT_EQ(firstResolved->key, "add");
    EXPECT_EQ(firstResolved->resolutionStatus, "resolved");
    EXPECT_FALSE(firstResolved->address.empty());
}

TEST(CartographerToolTest, StandaloneParserAndResolverProcessesWorkIndependently) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string libraryPath = (buildDir / "libagentmath_poc.so").string();
    const std::string parseExe = (buildDir / "cartographer_parse").string();
    const std::string resolveExe = (buildDir / "cartographer_resolve").string();
    const std::filesystem::path schemaPath = buildDir / "standalone_parser_output.json";

    ProcessResult parseResult = runProcess(makeArgList({parseExe, headerPath}));
    ASSERT_EQ(parseResult.exitCode, 0) << parseResult.stderrText;
    EXPECT_TRUE(parseResult.stderrText.empty());

    Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(protocol::decodeParseDescription(parseResult.stdoutText, description, error)) << error;
    ASSERT_TRUE(description.hasSymbols());
    CPtr<Mapper::NodeDescription> firstDescription = description.firstSymbol();
    ASSERT_TRUE(firstDescription);
    EXPECT_EQ(firstDescription->key, "add");

    {
        std::ofstream schemaFile(schemaPath);
        ASSERT_TRUE(schemaFile.is_open());
        schemaFile << parseResult.stdoutText;
    }

    ProcessResult resolveResult = runProcess(makeArgList({resolveExe, libraryPath, schemaPath.string()}));
    ASSERT_EQ(resolveResult.exitCode, 0) << resolveResult.stderrText;
    EXPECT_TRUE(resolveResult.stderrText.empty());

    resolver::ResolvedApi resolved;
    ASSERT_TRUE(resolver::decodeResolvedApi(resolveResult.stdoutText, resolved, error)) << error;
    EXPECT_EQ(resolved.libraryPath, libraryPath);
    EXPECT_EQ(resolved.parserSchemaFormat, protocol::parserSchemaFormatName());
    ASSERT_TRUE(resolved.hasSymbols());
    CPtr<resolver::ResolvedSymbol> firstResolved = resolved.firstSymbol();
    ASSERT_TRUE(firstResolved);
    EXPECT_EQ(firstResolved->key, "add");
    EXPECT_EQ(firstResolved->resolutionStatus, "resolved");
    EXPECT_FALSE(firstResolved->address.empty());
}

TEST(CartographerToolTest, StandaloneParserProcessReportsUsageAndJsonShape) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string parseExe = (buildDir / "cartographer_parse").string();

    ProcessResult usageResult = runProcess(makeArgList({parseExe}));
    EXPECT_EQ(usageResult.exitCode, 1);
    EXPECT_TRUE(usageResult.stdoutText.empty());
    EXPECT_EQ(usageResult.stderrText, "Usage: cartographer_parse <header-path>\n");

    ProcessResult parseResult = runProcess(makeArgList({parseExe, headerPath}));
    ASSERT_EQ(parseResult.exitCode, 0) << parseResult.stderrText;
    EXPECT_TRUE(parseResult.stderrText.empty());
    ASSERT_FALSE(parseResult.stdoutText.empty());
    EXPECT_EQ(parseResult.stdoutText.front(), '{');
    EXPECT_EQ(parseResult.stdoutText.back(), '\n');
    EXPECT_NE(parseResult.stdoutText.find("\"symbols\":"), std::string::npos);
    EXPECT_NE(parseResult.stdoutText.find("\"key\":\"add\""), std::string::npos);
}

TEST(CartographerToolTest, StandaloneResolverProcessReportsUsageAndResolvedJsonShape) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string libraryPath = (buildDir / "libagentmath_poc.so").string();
    const std::string parseExe = (buildDir / "cartographer_parse").string();
    const std::string resolveExe = (buildDir / "cartographer_resolve").string();

    ProcessResult usageResult = runProcess(makeArgList({resolveExe}));
    EXPECT_EQ(usageResult.exitCode, 1);
    EXPECT_TRUE(usageResult.stdoutText.empty());
    EXPECT_EQ(usageResult.stderrText,
              "Usage: cartographer_resolve [--no-addresses] <library-path> <api-schema-json-path|->\n");

    ProcessResult parseResult = runProcess(makeArgList({parseExe, headerPath}));
    ASSERT_EQ(parseResult.exitCode, 0) << parseResult.stderrText;

    ProcessResult resolveResult = runProcess(makeArgList({resolveExe, libraryPath, "-"}), parseResult.stdoutText);
    ASSERT_EQ(resolveResult.exitCode, 0) << resolveResult.stderrText;
    EXPECT_TRUE(resolveResult.stderrText.empty());
    ASSERT_FALSE(resolveResult.stdoutText.empty());
    EXPECT_EQ(resolveResult.stdoutText.front(), '{');
    EXPECT_EQ(resolveResult.stdoutText.back(), '\n');
    EXPECT_NE(resolveResult.stdoutText.find("\"format\":\"resolver_json_v1\""), std::string::npos);
    EXPECT_NE(resolveResult.stdoutText.find("\"api_schema_format\":\"parser_json_v1\""), std::string::npos);
    EXPECT_NE(resolveResult.stdoutText.find("\"resolution_status\":\"resolved\""), std::string::npos);
}

TEST(CartographerToolTest, ResolvedApiRoundTripsLibraryMetadataAndSymbols) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string libraryPath = (buildDir / "libagentmath_poc.so").string();

    Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(parser::parseHeaderToDescription(headerPath, description, error)) << error;

    resolver::ResolvedApi original;
    ASSERT_TRUE(resolver::resolveApiDescription(libraryPath, description, original, error)) << error;

    // Encode then decode
    const std::string json = resolver::encodeResolvedApi(original);
    resolver::ResolvedApi decoded;
    ASSERT_TRUE(resolver::decodeResolvedApi(json, decoded, error)) << error;

    // Library metadata should round-trip exactly
    EXPECT_EQ(decoded.libraryPath, original.libraryPath);
    EXPECT_EQ(decoded.fileSize, original.fileSize);
    EXPECT_EQ(decoded.modifiedTimeNs, original.modifiedTimeNs);
    EXPECT_EQ(decoded.contentHash, original.contentHash);
    EXPECT_EQ(decoded.addressBindingsProcessLocal, original.addressBindingsProcessLocal);

    // Summary counts
    EXPECT_EQ(decoded.symbolCount, original.symbolCount);
    EXPECT_EQ(decoded.resolvedCount, original.resolvedCount);
    EXPECT_EQ(decoded.unresolvedCount, original.unresolvedCount);

    // Schema format fields
    EXPECT_EQ(decoded.parserSchemaFormat, original.parserSchemaFormat);
    EXPECT_FALSE(decoded.parserSchemaJson.empty());

    // Symbols
    ASSERT_TRUE(decoded.hasSymbols());
    CPtr<resolver::ResolvedSymbol> sym = decoded.firstSymbol();
    ASSERT_TRUE(sym);
    EXPECT_EQ(sym->key, "add");
    EXPECT_EQ(sym->resolutionStatus, "resolved");
    // Address is process-local so just check it's non-empty after encode-decode
    EXPECT_FALSE(sym->address.empty());

    // Re-encode decoded and verify stability (second encode should equal first)
    const std::string reEncoded = resolver::encodeResolvedApi(decoded);
    EXPECT_EQ(reEncoded, json);
}

TEST(CartographerToolTest, ResolvedApiRoundTripsWithAddressesStripped) {
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string libraryPath = (buildDir / "libagentmath_poc.so").string();

    Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(parser::parseHeaderToDescription(headerPath, description, error)) << error;

    resolver::ResolvedApi original;
    ASSERT_TRUE(resolver::resolveApiDescription(libraryPath, description, original, error,
                                                /*includeAddresses=*/false)) << error;

    EXPECT_FALSE(original.addressBindingsProcessLocal);

    const std::string json = resolver::encodeResolvedApi(original);
    resolver::ResolvedApi decoded;
    ASSERT_TRUE(resolver::decodeResolvedApi(json, decoded, error)) << error;

    EXPECT_FALSE(decoded.addressBindingsProcessLocal);
    ASSERT_TRUE(decoded.hasSymbols());
    CPtr<resolver::ResolvedSymbol> sym = decoded.firstSymbol();
    ASSERT_TRUE(sym);
    EXPECT_EQ(sym->key, "add");
    EXPECT_EQ(sym->resolutionStatus, "resolved");
    // No address when stripped
    EXPECT_TRUE(sym->address.empty());

    // Stability check
    EXPECT_EQ(resolver::encodeResolvedApi(decoded), json);
}

TEST(CartographerToolTest, ResolvedApiParserSchemaJsonEmbedRoundTrips) {
    // Verify that the api_schema_json embedded inside the resolver JSON
    // can itself be decoded back to the original ParseDescription.
    const std::filesystem::path sourceDir(std::string(TEST_SOURCE_DIR));
    const std::filesystem::path buildDir(std::string(TEST_BUILD_DIR));
    const std::string headerPath = (sourceDir / "libagentmath_poc.h").string();
    const std::string libraryPath = (buildDir / "libagentmath_poc.so").string();

    Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(parser::parseHeaderToDescription(headerPath, description, error)) << error;

    resolver::ResolvedApi resolved;
    ASSERT_TRUE(resolver::resolveApiDescription(libraryPath, description, resolved, error)) << error;

    const std::string json = resolver::encodeResolvedApi(resolved);
    resolver::ResolvedApi decoded;
    ASSERT_TRUE(resolver::decodeResolvedApi(json, decoded, error)) << error;

    // The embedded api_schema_json should be valid parser JSON
    Mapper::ParseDescription embeddedDescription;
    ASSERT_TRUE(protocol::decodeParseDescription(decoded.parserSchemaJson, embeddedDescription, error)) << error;

    ASSERT_TRUE(embeddedDescription.hasSymbols());
    CPtr<Mapper::NodeDescription> firstSym = embeddedDescription.firstSymbol();
    ASSERT_TRUE(firstSym);
    EXPECT_EQ(firstSym->key, "add");
}

TEST(CartographerToolTest, ResolvedApiDecodeRejectsUnknownFormat) {
    const std::string badJson =
        "{\"format\":\"resolver_json_v99\","
        "\"library\":{\"path\":\"/lib/libfoo.so\","
        "\"file_size\":1234,\"modified_time_ns\":9999,"
        "\"content_hash\":\"fnv1a64:0000000000000000\","
        "\"address_bindings_process_local\":false},"
        "\"summary\":{\"symbol_count\":0,\"resolved_count\":0,\"unresolved_count\":0},"
        "\"api_schema_format\":\"parser_json_v1\","
        "\"api_schema_json\":\"{\\\"symbols\\\":[]}\","
        "\"symbols\":[]}";

    resolver::ResolvedApi out;
    std::string error;
    EXPECT_FALSE(resolver::decodeResolvedApi(badJson, out, error));
    EXPECT_FALSE(error.empty());
}

TEST(CartographerToolTest, ResolvedApiDecodeRejectsMissingApiSchemaJson) {
    const std::string badJson =
        "{\"format\":\"resolver_json_v1\","
        "\"library\":{\"path\":\"/lib/libfoo.so\","
        "\"file_size\":1234,\"modified_time_ns\":9999,"
        "\"content_hash\":\"fnv1a64:0000000000000000\","
        "\"address_bindings_process_local\":false},"
        "\"summary\":{\"symbol_count\":0,\"resolved_count\":0,\"unresolved_count\":0},"
        "\"api_schema_format\":\"parser_json_v1\","
        "\"api_schema_json\":\"\","
        "\"symbols\":[]}";

    resolver::ResolvedApi out;
    std::string error;
    EXPECT_FALSE(resolver::decodeResolvedApi(badJson, out, error));
    EXPECT_NE(error.find("missing api_schema_json"), std::string::npos);
}
