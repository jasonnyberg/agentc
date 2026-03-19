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
#include "../service.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace agentc::cartographer;
using namespace agentc;

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

static std::string readStringField(CPtr<ListreeValue> node, const std::string& key) {
    auto item = node ? node->find(key) : nullptr;
    auto value = item ? item->getValue(false, false) : nullptr;
    if (!value || !value->getData() || value->getLength() == 0) {
        return {};
    }
    return std::string(static_cast<char*>(value->getData()), value->getLength());
}

TEST(CartographerServiceTest, ImportReturnsSelfContainedDefinitions) {
    Mapper mapper;
    FFI ffi;
    CartographerService service(mapper, ffi);

    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    ImportRequest request;
    request.libraryPath = (buildDir / "libagentmath_poc.so").string();
    request.headerPath = (sourceDir / "libagentmath_poc.h").string();
    request.scopeName = "defs";
    request.requestId = "cartographer-test-sync";

    ImportResult result = service.import(request);
    ASSERT_TRUE(result.ok);

    auto defs = result.definitions;
    ASSERT_TRUE(bool(defs));

    auto addItem = defs->find("add");
    ASSERT_TRUE(bool(addItem));
    auto addDef = addItem->getValue(false, false);
    ASSERT_TRUE(bool(addDef));
    EXPECT_EQ(readStringField(addDef, "safety"), "safe");
    EXPECT_EQ(readStringField(addDef, "imported_via"), "cartographer_service");

    auto metaItem = defs->find("__cartographer");
    ASSERT_TRUE(bool(metaItem));
    auto metadata = metaItem->getValue(false, false);
    ASSERT_TRUE(bool(metadata));
    EXPECT_EQ(readStringField(metadata, "scope"), "defs");
    EXPECT_EQ(readStringField(metadata, "library"), request.libraryPath);
    EXPECT_EQ(readStringField(metadata, "header"), request.headerPath);
    EXPECT_EQ(readStringField(metadata, "execution_mode"), "sync");
    EXPECT_EQ(readStringField(metadata, "protocol"), "protocol_v1");
    EXPECT_EQ(readStringField(metadata, "api_schema_format"), "parser_json_v1");
    EXPECT_EQ(readStringField(metadata, "binding_mode"), "programmer_managed");
    EXPECT_EQ(readStringField(metadata, "requested_name"), request.scopeName);
    EXPECT_EQ(readStringField(metadata, "request_id"), request.requestId);
    EXPECT_EQ(readStringField(metadata, "status"), "ready");
    EXPECT_FALSE(readStringField(metadata, "symbol_count").empty());
}

TEST(CartographerServiceTest, DeferredImportQueuesHandleAndCollectsImportObject) {
    Mapper mapper;
    FFI ffi;
    CartographerService service(mapper, ffi);

    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    ImportRequest request;
    request.libraryPath = (buildDir / "libagentmath_poc.so").string();
    request.headerPath = (sourceDir / "libagentmath_poc.h").string();
    request.scopeName = "defs";
    request.executionMode = ImportExecutionMode::Deferred;
    request.requestId = "cartographer-test-deferred";

    ImportResult result = service.import(request);
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.executionMode, ImportExecutionMode::Deferred);
    EXPECT_EQ(result.requestId, request.requestId);
    EXPECT_EQ(result.status, "queued");
    EXPECT_EQ(readStringField(result.definitions, "status"), "queued");
    EXPECT_EQ(readStringField(result.definitions, "protocol"), "protocol_v1");
    EXPECT_EQ(readStringField(result.definitions, "api_schema_format"), "parser_json_v1");
    EXPECT_EQ(readStringField(result.definitions, "service_boundary"), "subprocess_pipe");
    EXPECT_EQ(readStringField(result.definitions, "response_owner"), "vm_collect");

    ImportResult status;
    bool ready = false;
    for (int i = 0; i < 200; ++i) {
        status = service.importStatus(result.requestId);
        ASSERT_FALSE(status.status.empty());
        if (status.status == "ready") {
            ready = true;
            break;
        }
        ASSERT_TRUE(status.status == "queued" || status.status == "running") << status.status;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(ready) << "Deferred import never became ready";
    EXPECT_EQ(readStringField(status.definitions, "status"), "ready");
    EXPECT_EQ(readStringField(status.definitions, "request_id"), request.requestId);
    EXPECT_EQ(readStringField(status.definitions, "protocol"), "protocol_v1");
    EXPECT_EQ(readStringField(status.definitions, "api_schema_format"), "parser_json_v1");

    ImportResult collected = service.collect(result.requestId);
    ASSERT_TRUE(collected.ok);
    EXPECT_EQ(collected.status, "ready");

    auto defs = collected.definitions;
    ASSERT_TRUE(bool(defs));

    auto metaItem = defs->find("__cartographer");
    ASSERT_TRUE(bool(metaItem));
    auto metadata = metaItem->getValue(false, false);
    ASSERT_TRUE(bool(metadata));
    EXPECT_EQ(readStringField(metadata, "execution_mode"), "deferred");
    EXPECT_EQ(readStringField(metadata, "request_id"), request.requestId);
    EXPECT_EQ(readStringField(metadata, "status"), "ready");
    EXPECT_EQ(readStringField(metadata, "protocol"), "protocol_v1");
    EXPECT_EQ(readStringField(metadata, "api_schema_format"), "parser_json_v1");
    EXPECT_EQ(readStringField(metadata, "service_boundary"), "subprocess_pipe");
    EXPECT_EQ(readStringField(metadata, "response_owner"), "vm_collect");
    EXPECT_EQ(readStringField(metadata, "binding_mode"), "programmer_managed");
    EXPECT_EQ(readStringField(metadata, "requested_name"), request.scopeName);
}

TEST(CartographerServiceTest, DeferredImportSurfacesImportFailureStatus) {
    Mapper mapper;
    FFI ffi;
    CartographerService service(mapper, ffi);

    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    ImportRequest request;
    request.libraryPath = "/definitely/missing/libagentmath_poc.so";
    request.headerPath = (sourceDir / "libagentmath_poc.h").string();
    request.scopeName = "defs";
    request.executionMode = ImportExecutionMode::Deferred;
    request.requestId = "cartographer-test-import-failure";

    ImportResult queued = service.import(request);
    ASSERT_TRUE(queued.ok) << queued.error;
    EXPECT_EQ(queued.status, "queued");

    ImportResult status;
    bool failed = false;
    for (int i = 0; i < 200; ++i) {
        status = service.importStatus(request.requestId);
        ASSERT_FALSE(status.status.empty());
        if (status.status == "import_failed") {
            failed = true;
            break;
        }
        ASSERT_TRUE(status.status == "queued" || status.status == "running") << status.status;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(failed) << "Deferred import never reported import_failed";
    EXPECT_EQ(readStringField(status.definitions, "status"), "import_failed");
    EXPECT_NE(status.error.find("Failed to load library"), std::string::npos);

    ImportResult collected = service.collect(request.requestId);
    ASSERT_FALSE(collected.ok);
    EXPECT_EQ(collected.status, "import_failed");
    EXPECT_NE(collected.error.find("Failed to load library"), std::string::npos);
}

TEST(CartographerServiceTest, DeferredImportSurfacesTransportFailureStatus) {
    Mapper mapper;
    FFI ffi;
    CartographerService service(mapper, ffi);

    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    ImportRequest request;
    request.libraryPath = (buildDir / "libagentmath_poc.so").string();
    request.headerPath = (sourceDir / "libagentmath_poc.h").string();
    request.scopeName = "defs";
    request.executionMode = ImportExecutionMode::Deferred;
    request.requestId = "cartographer-test-transport-failure";

    ImportResult queued = service.import(request);
    ASSERT_TRUE(queued.ok) << queued.error;
    service.terminateSubprocess();

    ImportResult status;
    bool failed = false;
    for (int i = 0; i < 200; ++i) {
        status = service.importStatus(request.requestId);
        ASSERT_FALSE(status.status.empty());
        if (status.status == "transport_failed") {
            failed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(failed) << "Deferred import never reported transport_failed";
    EXPECT_EQ(readStringField(status.definitions, "status"), "transport_failed");
    EXPECT_EQ(status.error, "Cartographer subprocess disconnected");

    ImportResult collected = service.collect(request.requestId);
    ASSERT_FALSE(collected.ok);
    EXPECT_EQ(collected.status, "transport_failed");
    EXPECT_EQ(collected.error, "Cartographer subprocess disconnected");
}

TEST(CartographerProtocolTest, EdictProtocolRoundTripsRequestAndStatus) {
    ImportRequest request;
    request.libraryPath = "/tmp/libagent math.so";
    request.headerPath = "/tmp/agent math.h";
    request.scopeName = "defs";
    request.executionMode = ImportExecutionMode::Deferred;
    request.requestId = "request-42";

    const std::string requestMessage = protocol::encodeImportRequest(request);
    ImportRequest decodedRequest;
    std::string error;
    ASSERT_TRUE(protocol::decodeImportRequest(requestMessage, decodedRequest, error)) << error;
    EXPECT_EQ(decodedRequest.libraryPath, request.libraryPath);
    EXPECT_EQ(decodedRequest.headerPath, request.headerPath);
    EXPECT_EQ(decodedRequest.scopeName, request.scopeName);
    EXPECT_EQ(decodedRequest.executionMode, request.executionMode);
    EXPECT_EQ(decodedRequest.requestId, request.requestId);

    ImportResult status;
    status.executionMode = ImportExecutionMode::Deferred;
    status.requestId = request.requestId;
    status.scopeName = request.scopeName;
    status.status = "ready";
    status.symbolCount = 2;

    Mapper::ParseDescription description;
    Mapper::NodeDescription symbol;
    symbol.key = "add";
    symbol.kind = "Function";
    symbol.name = "add";
    symbol.returnType = "int";
    Mapper::NodeDescription parameter;
    parameter.key = "p0";
    parameter.kind = "Parameter";
    parameter.type = "int";
    symbol.appendChild(parameter);
    description.appendSymbol(symbol);

    const std::string statusMessage = protocol::encodeImportStatus(request, status, &description);
    EXPECT_NE(statusMessage.find("api_schema_format \"parser_json_v1\""), std::string::npos);
    EXPECT_NE(statusMessage.find("api_schema_json \"{\\\"symbols\\\"") , std::string::npos);
    ImportRequest decodedStatusRequest;
    ImportResult decodedStatus;
    Mapper::ParseDescription decodedDescription;
    error.clear();
    ASSERT_TRUE(protocol::decodeImportStatus(statusMessage, decodedStatusRequest, decodedStatus, &decodedDescription, error)) << error;
    EXPECT_EQ(decodedStatusRequest.libraryPath, request.libraryPath);
    EXPECT_EQ(decodedStatusRequest.headerPath, request.headerPath);
    EXPECT_EQ(decodedStatusRequest.scopeName, request.scopeName);
    EXPECT_EQ(decodedStatus.requestId, request.requestId);
    EXPECT_EQ(decodedStatus.status, "ready");
    EXPECT_EQ(decodedStatus.symbolCount, 2u);
    ASSERT_EQ(decodedDescription.symbolCount(), 1u);
    CPtr<Mapper::NodeDescription> decodedSymbol = decodedDescription.firstSymbol();
    ASSERT_TRUE(decodedSymbol);
    EXPECT_EQ(decodedSymbol->key, "add");
    ASSERT_EQ(decodedSymbol->childCount(), 1u);
    CPtr<Mapper::NodeDescription> decodedParameter = decodedSymbol->firstChild();
    ASSERT_TRUE(decodedParameter);
    EXPECT_EQ(decodedParameter->type, "int");
}

TEST(CartographerServiceTest, ImportResolvedFileReturnsDefinitionsAndResolutionMetadata) {
    Mapper mapper;
    FFI ffi;
    CartographerService service(mapper, ffi);

    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path headerPath = sourceDir / "libagentmath_poc.h";
    const std::filesystem::path libraryPath = buildDir / "libagentmath_poc.so";
    const std::filesystem::path resolvedPath = buildDir / "cartographer_resolved_import_test.json";

    Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) << error;

    resolver::ResolvedApi resolved;
    ASSERT_TRUE(resolver::resolveApiDescription(libraryPath.string(), description, resolved, error)) << error;

    std::ofstream output(resolvedPath);
    ASSERT_TRUE(output.good());
    output << resolver::encodeResolvedApi(resolved);
    output.close();

    ImportResult result = service.importResolvedFile(resolvedPath.string(), "defs");
    ASSERT_TRUE(result.ok);

    auto defs = result.definitions;
    ASSERT_TRUE(bool(defs));

    auto addItem = defs->find("add");
    ASSERT_TRUE(bool(addItem));
    auto addDef = addItem->getValue(false, false);
    ASSERT_TRUE(bool(addDef));
    EXPECT_EQ(readStringField(addDef, "resolution_status"), "resolved");
    EXPECT_EQ(readStringField(addDef, "resolved_symbol"), "add");

    auto metaItem = defs->find("__cartographer");
    ASSERT_TRUE(bool(metaItem));
    auto metadata = metaItem->getValue(false, false);
    ASSERT_TRUE(bool(metadata));
    EXPECT_EQ(readStringField(metadata, "resolved_schema_format"), resolver::resolverSchemaFormatName());
    EXPECT_EQ(readStringField(metadata, "resolved_schema_path"), resolvedPath.string());
    EXPECT_EQ(readStringField(metadata, "api_schema_format"), protocol::parserSchemaFormatName());
    EXPECT_EQ(readStringField(metadata, "resolved_address_bindings_process_local"), "true");
    EXPECT_EQ(readStringField(metadata, "binding_mode"), "programmer_managed");
    EXPECT_EQ(readStringField(metadata, "requested_name"), "defs");
    EXPECT_FALSE(readStringField(metadata, "resolved_content_hash").empty());
}

TEST(CartographerServiceTest, ImportResolvedFileRejectsStaleArtifactFingerprint) {
    Mapper mapper;
    FFI ffi;
    CartographerService service(mapper, ffi);

    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path headerPath = sourceDir / "libagentmath_poc.h";
    const std::filesystem::path libraryPath = buildDir / "libagentmath_poc.so";
    const std::filesystem::path resolvedPath = buildDir / "cartographer_resolved_import_stale_test.json";

    Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) << error;

    resolver::ResolvedApi resolved;
    ASSERT_TRUE(resolver::resolveApiDescription(libraryPath.string(), description, resolved, error)) << error;
    resolved.fileSize += 1;

    std::ofstream output(resolvedPath);
    ASSERT_TRUE(output.good());
    output << resolver::encodeResolvedApi(resolved);
    output.close();

    ImportResult result = service.importResolvedFile(resolvedPath.string(), "defs");
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(result.status, "import_failed");
    EXPECT_EQ(result.error, "Resolved schema is stale for library: " + libraryPath.string());
}
