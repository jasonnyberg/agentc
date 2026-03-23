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

#pragma once

#include <string>
#include <memory>
#include "mapper.h"
#include "ffi.h"

namespace agentc {
namespace cartographer {

enum class ImportExecutionMode {
    Sync,
    Deferred,
};

struct ImportRequest {
    std::string libraryPath;
    std::string headerPath;
    ImportExecutionMode executionMode = ImportExecutionMode::Sync;
    std::string requestId;
};

struct ImportResult {
    bool ok = false;
    std::string error;
    CPtr<ListreeValue> definitions;
    size_t symbolCount = 0;
    ImportExecutionMode executionMode = ImportExecutionMode::Sync;
    std::string status;
    std::string requestId;
};

class CartographerService {
public:
    CartographerService(Mapper& mapper, FFI& ffi);
    ~CartographerService();

    void terminateSubprocess();

    ImportResult import(const ImportRequest& request);
    ImportResult importResolvedFile(const std::string& resolvedSchemaPath);
    ImportResult importResolverJson(const std::string& resolvedSchemaJson,
                                    const std::string& sourceLabel);
    ImportResult importDeferred(const ImportRequest& request);
    ImportResult importStatus(const std::string& requestId);
    ImportResult collect(const std::string& requestId);

private:
    struct Impl;

    Mapper& mapper;
    FFI& ffi;
    size_t nextDeferredRequestId = 1;
    std::unique_ptr<Impl> impl;

    static std::string safetyTagForFunction(const std::string& functionName);
    static const char* serviceBoundaryName();
    static const char* statusQueued();
    static const char* statusRunning();
    static const char* statusReady();
    static const char* statusImportFailed();
    static const char* statusStartupFailed();
    static const char* statusTransportFailed();
    static const char* statusWorkerDecodeFailed();
    static CPtr<ListreeValue> createImportHandleValue(const ImportResult& result,
                                                      const ImportRequest& request);
    ImportResult createStatusResult(const ImportRequest& request,
                                    const std::string& status,
                                    const std::string& error = std::string(),
                                    size_t symbolCount = 0) const;
    static bool isPendingStatus(const std::string& status);
    static bool isReadyStatus(const std::string& status);
    ImportResult performImport(const ImportRequest& request,
                               ImportExecutionMode reportedMode,
                               Mapper& mapperRef,
                               FFI& ffiRef,
                               Mapper::ParseDescription* descriptionOut = nullptr);
    ImportResult queueDeferredImport(const ImportRequest& request);
    bool processDeferredRequest(const std::string& requestId, ImportResult& result);
    static CPtr<ListreeValue> materializeImportedDefinitions(const Mapper::ParseDescription& description,
                                                             const ImportRequest& request,
                                                             ImportExecutionMode reportedMode,
                                                             size_t& symbolCountOut);
    std::string nextRequestId();
};

} // namespace cartographer
} // namespace agentc
