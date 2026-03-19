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

#include "service.h"

#include "parser.h"
#include "protocol.h"
#include "resolver.h"
#include "../core/container.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <iomanip>
#include <signal.h>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace agentc {
namespace cartographer {

namespace {

static const char* executionModeName(ImportExecutionMode mode) {
    switch (mode) {
        case ImportExecutionMode::Sync:
            return "sync";
        case ImportExecutionMode::Deferred:
            return "deferred";
    }
    return "sync";
}

static bool valueToString(CPtr<ListreeValue> value, std::string& out) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return false;
    }
    if ((value->getFlags() & LtvFlags::Binary) != LtvFlags::None) {
        return false;
    }
    out.assign(static_cast<char*>(value->getData()), value->getLength());
    return true;
}

static bool readFileContents(const std::string& path, std::string& out) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return true;
}

static std::string computeFileHash(const std::string& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Failed to open library for hashing: " + path;
        return {};
    }

    uint64_t hash = 1469598103934665603ULL;
    char buffer[4096];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ULL;
        }
    }
    if (!input.eof()) {
        error = "Failed while hashing library: " + path;
        return {};
    }

    std::ostringstream stream;
    stream << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

static bool readLibraryFingerprint(const std::string& path,
                                   uint64_t& fileSize,
                                   uint64_t& modifiedTimeNs,
                                   std::string& contentHash,
                                   std::string& error) {
    struct stat info;
    if (::stat(path.c_str(), &info) != 0) {
        error = "Failed to stat library: " + path;
        return false;
    }

    fileSize = static_cast<uint64_t>(info.st_size);
#if defined(__APPLE__)
    modifiedTimeNs = static_cast<uint64_t>(info.st_mtimespec.tv_sec) * 1000000000ULL +
                     static_cast<uint64_t>(info.st_mtimespec.tv_nsec);
#else
    modifiedTimeNs = static_cast<uint64_t>(info.st_mtim.tv_sec) * 1000000000ULL +
                     static_cast<uint64_t>(info.st_mtim.tv_nsec);
#endif
    contentHash = computeFileHash(path, error);
    return !contentHash.empty();
}

static bool validateResolvedArtifactFreshness(const resolver::ResolvedApi& resolved, std::string& error) {
    if (resolved.fileSize == 0 || resolved.modifiedTimeNs == 0 || resolved.contentHash.empty()) {
        error = "Resolved schema is missing freshness metadata";
        return false;
    }

    uint64_t currentFileSize = 0;
    uint64_t currentModifiedTimeNs = 0;
    std::string currentContentHash;
    if (!readLibraryFingerprint(resolved.libraryPath,
                                currentFileSize,
                                currentModifiedTimeNs,
                                currentContentHash,
                                error)) {
        return false;
    }

    if (currentFileSize != resolved.fileSize ||
        currentModifiedTimeNs != resolved.modifiedTimeNs ||
        currentContentHash != resolved.contentHash) {
        error = "Resolved schema is stale for library: " + resolved.libraryPath;
        return false;
    }
    return true;
}

static bool isUnsafeFunctionName(const std::string& functionName) {
    return functionName == "system" ||
           functionName == "free" ||
           functionName == "realloc" ||
           functionName == "dlclose" ||
           functionName == "fork" ||
           functionName == "execve" ||
           functionName == "unlink" ||
           functionName == "remove" ||
           functionName == "rename" ||
           functionName == "kill";
}

static void annotateImportedNode(CPtr<ListreeValue> value,
                                 const std::string& key,
                                 size_t& symbolCountOut) {
    if (!value) {
        return;
    }

    ++symbolCountOut;
    addNamedItem(value, "symbol", createStringValue(key));

    auto kindItem = value->find("kind");
    auto kindValue = kindItem ? kindItem->getValue(false, false) : nullptr;
    std::string kind;
    valueToString(kindValue, kind);
    if (kind == "Function") {
        const std::string safety = isUnsafeFunctionName(key) ? "unsafe" : "safe";
        addNamedItem(value, "safety", createStringValue(safety));
        addNamedItem(value, "imported_via", createStringValue("cartographer_service"));
        if (safety == "unsafe") {
            addNamedItem(value, "policy", createStringValue("blocked_by_default"));
        }
    }
}

static void annotateResolvedNode(CPtr<ListreeValue> value,
                                 const resolver::ResolvedSymbol& symbol) {
    if (!value) {
        return;
    }
    addNamedItem(value, "resolution_status", createStringValue(symbol.resolutionStatus));
    if (!symbol.symbolName.empty()) {
        addNamedItem(value, "resolved_symbol", createStringValue(symbol.symbolName));
    }
    if (!symbol.address.empty()) {
        addNamedItem(value, "resolver_address", createStringValue(symbol.address));
    }
}

static bool writeAll(int fd, const void* data, size_t size) {
    const char* bytes = static_cast<const char*>(data);
    while (size > 0) {
        ssize_t written = ::write(fd, bytes, size);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        bytes += static_cast<size_t>(written);
        size -= static_cast<size_t>(written);
    }
    return true;
}

static bool readAll(int fd, void* data, size_t size) {
    char* bytes = static_cast<char*>(data);
    while (size > 0) {
        ssize_t count = ::read(fd, bytes, size);
        if (count == 0) {
            return false;
        }
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        bytes += static_cast<size_t>(count);
        size -= static_cast<size_t>(count);
    }
    return true;
}

static bool writeFramedMessage(int fd, const std::string& message) {
    const uint64_t size = static_cast<uint64_t>(message.size());
    return writeAll(fd, &size, sizeof(size)) &&
           (message.empty() || writeAll(fd, message.data(), message.size()));
}

static bool readFramedMessage(int fd, std::string& message) {
    uint64_t size = 0;
    if (!readAll(fd, &size, sizeof(size))) {
        return false;
    }
    message.assign(static_cast<size_t>(size), '\0');
    return size == 0 || readAll(fd, &message[0], static_cast<size_t>(size));
}

template<typename T>
static CPtr<T> allocateArenaObject() {
    SlabId sid = Allocator<T>::getAllocator().allocate();
    return CPtr<T>(sid);
}

template<typename T>
static CPtr<AATree<T>> ensureTree(CPtr<AATree<T>>& tree) {
    if (!tree) {
        tree = allocateArenaObject<AATree<T>>();
    }
    return tree;
}

template<typename T>
static CPtr<AATree<T>> findTreeNode(CPtr<AATree<T>>& tree, const std::string& name) {
    if (!tree) {
        return nullptr;
    }
    return tree->find(name);
}

template<typename T>
static CPtr<T> findTreeValue(CPtr<AATree<T>>& tree, const std::string& name) {
    CPtr<AATree<T>> node = findTreeNode(tree, name);
    return node ? node->data : nullptr;
}

static const resolver::ResolvedSymbol* findResolvedSymbolByKey(const resolver::ResolvedApi& resolved,
                                                               const std::string& key) {
    const resolver::ResolvedSymbol* found = nullptr;
    resolved.forEachSymbol([&](CPtr<resolver::ResolvedSymbol>& symbol) {
        if (!found && symbol && symbol->key == key) {
            found = symbol.operator->();
        }
    });
    return found;
}

} // namespace

struct CartographerService::Impl {
    struct DeferredState {
        ImportRequest request;
        std::string requestMessage;
        std::string responseMessage;
        std::string status = "queued";
        std::string error;
        Mapper::ParseDescription description;
        size_t symbolCount = 0;
    };

    std::mutex mutex;
    CPtr<AATree<DeferredState>> requests;
    bool stopping = false;
    bool childAvailable = false;
    bool childExited = false;
    int toChildFd = -1;
    int fromChildFd = -1;
    pid_t childPid = -1;
    std::thread reader;

    Impl() = default;
};

const char* CartographerService::serviceBoundaryName() {
    return "subprocess_pipe";
}

const char* CartographerService::statusQueued() {
    return "queued";
}

const char* CartographerService::statusRunning() {
    return "running";
}

const char* CartographerService::statusReady() {
    return "ready";
}

const char* CartographerService::statusImportFailed() {
    return "import_failed";
}

const char* CartographerService::statusStartupFailed() {
    return "startup_failed";
}

const char* CartographerService::statusTransportFailed() {
    return "transport_failed";
}

const char* CartographerService::statusWorkerDecodeFailed() {
    return "worker_decode_failed";
}

bool CartographerService::isPendingStatus(const std::string& status) {
    return status == statusQueued() || status == statusRunning();
}

bool CartographerService::isReadyStatus(const std::string& status) {
    return status == statusReady();
}

CartographerService::CartographerService(Mapper& mapperRef, FFI& ffiRef)
    : mapper(mapperRef), ffi(ffiRef), impl(std::make_unique<Impl>()) {
    int parentToChild[2] = {-1, -1};
    int childToParent[2] = {-1, -1};
    if (::pipe(parentToChild) != 0 || ::pipe(childToParent) != 0) {
        if (parentToChild[0] >= 0) ::close(parentToChild[0]);
        if (parentToChild[1] >= 0) ::close(parentToChild[1]);
        if (childToParent[0] >= 0) ::close(childToParent[0]);
        if (childToParent[1] >= 0) ::close(childToParent[1]);
        return;
    }

    const pid_t pid = ::fork();
    if (pid == 0) {
        ::close(parentToChild[1]);
        ::close(childToParent[0]);

        Mapper workerMapper;
        FFI workerFfi;
        std::string requestMessage;
        while (readFramedMessage(parentToChild[0], requestMessage)) {
            ImportRequest request;
            std::string decodeError;
            if (!protocol::decodeImportRequest(requestMessage, request, decodeError)) {
                ImportRequest invalidRequest;
                invalidRequest.executionMode = ImportExecutionMode::Deferred;
                ImportResult failure;
                failure.executionMode = ImportExecutionMode::Deferred;
                failure.status = statusWorkerDecodeFailed();
                failure.error = "Failed to decode Cartographer protocol request: " + decodeError;
                writeFramedMessage(childToParent[1], protocol::encodeImportStatus(invalidRequest, failure));
                continue;
            }

            ImportResult running;
            running.ok = true;
            running.executionMode = ImportExecutionMode::Deferred;
            running.requestId = request.requestId;
            running.scopeName = request.scopeName;
            running.status = statusRunning();
            if (!writeFramedMessage(childToParent[1], protocol::encodeImportStatus(request, running))) {
                break;
            }

            Mapper::ParseDescription description;
            ImportResult result;
            result.executionMode = ImportExecutionMode::Deferred;
            result.requestId = request.requestId;
            result.scopeName = request.scopeName;

            if (request.libraryPath.empty()) {
                result.status = statusImportFailed();
                result.error = "Cartographer import requires a library path";
            } else if (request.headerPath.empty()) {
                result.status = statusImportFailed();
                result.error = "Cartographer import requires a header path";
            } else if (request.scopeName.empty()) {
                result.status = statusImportFailed();
                result.error = "Cartographer import requires a scope name";
            } else if (!workerFfi.loadLibrary(request.libraryPath)) {
                result.status = statusImportFailed();
                result.error = "Failed to load library: " + request.libraryPath;
            } else {
                std::string parseError;
                if (!parser::parseHeaderToDescription(workerMapper, request.headerPath, description, parseError)) {
                    result.status = statusImportFailed();
                    result.error = parseError;
                } else {
                    result.ok = true;
                    result.status = statusReady();
                    result.symbolCount = description.symbolCount();
                }
            }

            if (!writeFramedMessage(childToParent[1], protocol::encodeImportStatus(request, result, result.ok ? &description : nullptr))) {
                break;
            }
        }

        ::close(parentToChild[0]);
        ::close(childToParent[1]);
        std::_Exit(0);
    }

    ::close(parentToChild[0]);
    ::close(childToParent[1]);

    if (pid < 0) {
        ::close(parentToChild[1]);
        ::close(childToParent[0]);
        return;
    }

    impl->toChildFd = parentToChild[1];
    impl->fromChildFd = childToParent[0];
    impl->childPid = pid;
    impl->childAvailable = true;
    impl->reader = std::thread([this] {
        while (true) {
            std::string responseMessage;
            if (!readFramedMessage(impl->fromChildFd, responseMessage)) {
                break;
            }

            ImportRequest request;
            ImportResult response;
            Mapper::ParseDescription description;
            std::string decodeError;
            if (!protocol::decodeImportStatus(responseMessage, request, response, &description, decodeError)) {
                continue;
            }

            std::lock_guard<std::mutex> lock(impl->mutex);
            CPtr<Impl::DeferredState> state = findTreeValue(impl->requests, response.requestId);
            if (!state) {
                continue;
            }
            state->responseMessage = responseMessage;
            state->status = response.status;
            state->error = response.error;
            state->symbolCount = response.symbolCount;
            if (response.status == statusReady()) {
                state->description = std::move(description);
            }
        }

        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->childAvailable = false;
        impl->childExited = true;
        if (impl->requests) {
            impl->requests->forEach([&](const std::string&, CPtr<Impl::DeferredState>& state) {
                if (!state || isReadyStatus(state->status) || !isPendingStatus(state->status)) {
                    return;
                }
                ImportResult failed;
                failed.executionMode = ImportExecutionMode::Deferred;
                failed.requestId = state->request.requestId;
                failed.scopeName = state->request.scopeName;
                failed.status = statusTransportFailed();
                failed.error = "Cartographer subprocess disconnected";
                state->status = failed.status;
                state->error = failed.error;
                state->responseMessage = protocol::encodeImportStatus(state->request, failed);
            });
        }
    });
}

CartographerService::~CartographerService() {
    terminateSubprocess();
}

void CartographerService::terminateSubprocess() {
    if (!impl) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->stopping = true;
    }
    if (impl->toChildFd >= 0) {
        if (impl->childPid > 0) {
            ::kill(impl->childPid, SIGTERM);
        }
        ::close(impl->toChildFd);
        impl->toChildFd = -1;
    }
    if (impl->reader.joinable()) {
        impl->reader.join();
    }
    if (impl->fromChildFd >= 0) {
        ::close(impl->fromChildFd);
        impl->fromChildFd = -1;
    }
    if (impl->childPid > 0) {
        int status = 0;
        ::waitpid(impl->childPid, &status, 0);
        impl->childPid = -1;
    }
}

std::string CartographerService::safetyTagForFunction(const std::string& functionName) {
    return isUnsafeFunctionName(functionName) ? "unsafe" : "safe";
}

CPtr<ListreeValue> CartographerService::createImportHandleValue(const ImportResult& result,
                                                                const ImportRequest& request) {
    auto handle = createNullValue();
    addNamedItem(handle, "library", createStringValue(request.libraryPath));
    addNamedItem(handle, "header", createStringValue(request.headerPath));
    addNamedItem(handle, "scope", createStringValue(request.scopeName));
    addNamedItem(handle, "execution_mode", createStringValue(executionModeName(result.executionMode)));
    addNamedItem(handle, "protocol", createStringValue(protocol::versionName()));
    addNamedItem(handle, "api_schema_format", createStringValue(protocol::parserSchemaFormatName()));
    addNamedItem(handle, "service_boundary", createStringValue(serviceBoundaryName()));
    addNamedItem(handle, "response_owner", createStringValue("vm_collect"));
    addNamedItem(handle, "binding_mode", createStringValue("programmer_managed"));
    addNamedItem(handle, "requested_name", createStringValue(request.scopeName));
    addNamedItem(handle, "status", createStringValue(result.status.empty() ? "queued" : result.status));
    if (!result.requestId.empty()) {
        addNamedItem(handle, "request_id", createStringValue(result.requestId));
    }
    if (result.symbolCount > 0) {
        addNamedItem(handle, "symbol_count", createStringValue(std::to_string(result.symbolCount)));
    }
    if (!result.error.empty()) {
        addNamedItem(handle, "error", createStringValue(result.error));
    }
    return handle;
}

ImportResult CartographerService::createStatusResult(const ImportRequest& request,
                                                     const std::string& status,
                                                     const std::string& error,
                                                     size_t symbolCount) const {
    ImportResult result;
    result.ok = isPendingStatus(status) || isReadyStatus(status);
    result.executionMode = request.executionMode;
    result.status = status;
    result.requestId = request.requestId;
    result.scopeName = request.scopeName;
    result.error = error;
    result.symbolCount = symbolCount;
    result.definitions = createImportHandleValue(result, request);
    return result;
}

std::string CartographerService::nextRequestId() {
    return "cartographer-request-" + std::to_string(nextDeferredRequestId++);
}

CPtr<ListreeValue> CartographerService::materializeImportedDefinitions(const Mapper::ParseDescription& description,
                                                                      const ImportRequest& request,
                                                                      ImportExecutionMode reportedMode,
                                                                      size_t& symbolCountOut) {
    auto definitions = Mapper::materialize(description);
    if (!definitions) {
        return nullptr;
    }

    symbolCountOut = 0;
    definitions->forEachTree([&](const std::string& key, CPtr<ListreeItem>& item) {
        if (!item) {
            return;
        }
        annotateImportedNode(item->getValue(false, false), key, symbolCountOut);
    });

    auto metadata = createNullValue();
    addNamedItem(metadata, "library", createStringValue(request.libraryPath));
    addNamedItem(metadata, "header", createStringValue(request.headerPath));
    addNamedItem(metadata, "scope", createStringValue(request.scopeName));
    addNamedItem(metadata, "execution_mode", createStringValue(executionModeName(reportedMode)));
    addNamedItem(metadata, "protocol", createStringValue(protocol::versionName()));
    addNamedItem(metadata, "api_schema_format", createStringValue(protocol::parserSchemaFormatName()));
    addNamedItem(metadata, "service_boundary", createStringValue(serviceBoundaryName()));
    addNamedItem(metadata, "response_owner", createStringValue("vm_collect"));
    addNamedItem(metadata, "binding_mode", createStringValue("programmer_managed"));
    addNamedItem(metadata, "requested_name", createStringValue(request.scopeName));
    addNamedItem(metadata, "status", createStringValue(statusReady()));
    if (!request.requestId.empty()) {
        addNamedItem(metadata, "request_id", createStringValue(request.requestId));
    }
    addNamedItem(metadata, "symbol_count", createStringValue(std::to_string(symbolCountOut)));
    addNamedItem(definitions, "__cartographer", metadata);
    return definitions;
}

ImportResult CartographerService::performImport(const ImportRequest& request,
                                                ImportExecutionMode reportedMode,
                                                Mapper& mapperRef,
                                                FFI& ffiRef,
                                                Mapper::ParseDescription* descriptionOut) {
    ImportResult result;
    result.executionMode = reportedMode;
    result.requestId = request.requestId;
    result.scopeName = request.scopeName;

    if (request.libraryPath.empty()) {
        result.error = "Cartographer import requires a library path";
        result.status = statusImportFailed();
        return result;
    }
    if (request.headerPath.empty()) {
        result.error = "Cartographer import requires a header path";
        result.status = statusImportFailed();
        return result;
    }
    if (request.scopeName.empty()) {
        result.error = "Cartographer import requires a scope name";
        result.status = statusImportFailed();
        return result;
    }

    if (!ffiRef.loadLibrary(request.libraryPath)) {
        result.error = "Failed to load library: " + request.libraryPath;
        result.status = statusImportFailed();
        return result;
    }

    Mapper::ParseDescription description;
    if (!mapperRef.parseDescription(request.headerPath, description)) {
        result.error = "Failed to map header: " + request.headerPath;
        result.status = statusImportFailed();
        return result;
    }

    result.ok = true;
    result.status = statusReady();
    result.symbolCount = description.symbolCount();
    if (descriptionOut) {
        *descriptionOut = std::move(description);
    }
    return result;
}

ImportResult CartographerService::queueDeferredImport(const ImportRequest& request) {
    ImportRequest queuedRequest = request;
    queuedRequest.executionMode = ImportExecutionMode::Deferred;
    queuedRequest.requestId = request.requestId.empty() ? nextRequestId() : request.requestId;

    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        CPtr<AATree<Impl::DeferredState>> requests = ensureTree(impl->requests);
        CPtr<Impl::DeferredState> state = findTreeValue(requests, queuedRequest.requestId);
        if (!state) {
            state = allocateArenaObject<Impl::DeferredState>();
            requests->add(queuedRequest.requestId, state);
        }
        state->request = queuedRequest;
        state->requestMessage = protocol::encodeImportRequest(queuedRequest);
        state->status = statusQueued();
        state->error.clear();
        state->description = {};
        state->symbolCount = 0;
        state->responseMessage = protocol::encodeImportStatus(queuedRequest, createStatusResult(queuedRequest, statusQueued()));
    }

    bool childAvailable = false;
    bool childExited = false;
    pid_t childPid = -1;
    int toChildFd = -1;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        childAvailable = impl->childAvailable;
        childExited = impl->childExited;
        childPid = impl->childPid;
        toChildFd = impl->toChildFd;
    }
    if (!childAvailable || toChildFd < 0 || !writeFramedMessage(toChildFd, protocol::encodeImportRequest(queuedRequest))) {
        const std::string failureStatus = (childExited || childPid > 0)
            ? statusTransportFailed()
            : statusStartupFailed();
        const std::string failureError = (failureStatus == statusTransportFailed())
            ? "Cartographer subprocess transport is unavailable"
            : "Cartographer subprocess failed to start";
        std::lock_guard<std::mutex> lock(impl->mutex);
        CPtr<Impl::DeferredState> state = findTreeValue(impl->requests, queuedRequest.requestId);
        if (state) {
            ImportResult failed = createStatusResult(queuedRequest, failureStatus, failureError);
            failed.ok = false;
            state->status = failed.status;
            state->error = failed.error;
            state->responseMessage = protocol::encodeImportStatus(queuedRequest, failed);
        }
        return createStatusResult(queuedRequest, failureStatus, failureError);
    }

    return createStatusResult(queuedRequest, statusQueued());
}

ImportResult CartographerService::import(const ImportRequest& request) {
    if (request.executionMode == ImportExecutionMode::Deferred) {
        return importDeferred(request);
    }

    ImportRequest syncRequest = request;
    syncRequest.executionMode = ImportExecutionMode::Sync;
    Mapper::ParseDescription description;
    ImportResult result = performImport(syncRequest, ImportExecutionMode::Sync, mapper, ffi, &description);
    if (!result.ok) {
        result.definitions = createImportHandleValue(result, syncRequest);
        return result;
    }

    result.definitions = materializeImportedDefinitions(description, syncRequest, ImportExecutionMode::Sync, result.symbolCount);
    if (!result.definitions) {
        result.ok = false;
        result.status = statusImportFailed();
        result.error = "Failed to materialize cartographer import definitions";
        result.definitions = createImportHandleValue(result, syncRequest);
    }
    return result;
}

ImportResult CartographerService::importResolvedFile(const std::string& resolvedSchemaPath,
                                                    const std::string& scopeName) {
    ImportResult result;
    result.executionMode = ImportExecutionMode::Sync;
    result.scopeName = scopeName;

    if (resolvedSchemaPath.empty()) {
        result.status = statusImportFailed();
        result.error = "Cartographer resolved import requires a resolved schema path";
        return result;
    }
    if (scopeName.empty()) {
        result.status = statusImportFailed();
        result.error = "Cartographer resolved import requires a scope name";
        return result;
    }

    std::string resolvedJson;
    if (!readFileContents(resolvedSchemaPath, resolvedJson)) {
        result.status = statusImportFailed();
        result.error = "Failed to read resolved schema file: " + resolvedSchemaPath;
        return result;
    }

    return importResolverJson(resolvedJson, scopeName, resolvedSchemaPath);
}

ImportResult CartographerService::importResolverJson(const std::string& resolvedSchemaJson,
                                                    const std::string& scopeName,
                                                    const std::string& sourceLabel) {
    ImportResult result;
    result.executionMode = ImportExecutionMode::Sync;
    result.scopeName = scopeName;

    if (resolvedSchemaJson.empty()) {
        result.status = statusImportFailed();
        result.error = "Cartographer resolved import requires resolved schema JSON";
        return result;
    }
    if (scopeName.empty()) {
        result.status = statusImportFailed();
        result.error = "Cartographer resolved import requires a scope name";
        return result;
    }

    resolver::ResolvedApi resolved;
    std::string decodeError;
    if (!resolver::decodeResolvedApi(resolvedSchemaJson, resolved, decodeError)) {
        result.status = statusImportFailed();
        result.error = decodeError;
        return result;
    }
    if (resolved.libraryPath.empty()) {
        result.status = statusImportFailed();
        result.error = "Resolved schema is missing a library path";
        return result;
    }
    if (!validateResolvedArtifactFreshness(resolved, decodeError)) {
        result.status = statusImportFailed();
        result.error = decodeError;
        return result;
    }

    Mapper::ParseDescription description;
    if (!protocol::decodeParseDescription(resolved.parserSchemaJson, description, decodeError)) {
        result.status = statusImportFailed();
        result.error = decodeError;
        return result;
    }
    if (!ffi.loadLibrary(resolved.libraryPath)) {
        result.status = statusImportFailed();
        result.error = "Failed to load library: " + resolved.libraryPath;
        return result;
    }

    ImportRequest request;
    request.libraryPath = resolved.libraryPath;
    request.headerPath = sourceLabel;
    request.scopeName = scopeName;

    result.ok = true;
    result.status = statusReady();
    result.symbolCount = description.symbolCount();
    result.definitions = materializeImportedDefinitions(description, request, ImportExecutionMode::Sync, result.symbolCount);
    if (!result.definitions) {
        result.ok = false;
        result.status = statusImportFailed();
        result.error = "Failed to materialize cartographer resolved import definitions";
        result.definitions = createImportHandleValue(result, request);
        return result;
    }

    result.definitions->forEachTree([&](const std::string& key, CPtr<ListreeItem>& item) {
        if (!item) {
            return;
        }
        const resolver::ResolvedSymbol* symbol = findResolvedSymbolByKey(resolved, key);
        if (symbol) {
            annotateResolvedNode(item->getValue(false, false), *symbol);
        }
    });

    auto metaItem = result.definitions->find("__cartographer");
    auto metadata = metaItem ? metaItem->getValue(false, false) : nullptr;
    if (metadata) {
        addNamedItem(metadata, "resolved_schema_format", createStringValue(resolver::resolverSchemaFormatName()));
        addNamedItem(metadata, "resolved_schema_path", createStringValue(sourceLabel));
        addNamedItem(metadata, "resolved_content_hash", createStringValue(resolved.contentHash));
        addNamedItem(metadata, "resolved_file_size", createStringValue(std::to_string(resolved.fileSize)));
        addNamedItem(metadata, "resolved_modified_time_ns", createStringValue(std::to_string(resolved.modifiedTimeNs)));
        addNamedItem(metadata, "resolved_address_bindings_process_local",
                     createStringValue(resolved.addressBindingsProcessLocal ? "true" : "false"));
    }
    return result;
}

ImportResult CartographerService::importDeferred(const ImportRequest& request) {
    if (request.libraryPath.empty() || request.headerPath.empty() || request.scopeName.empty()) {
        ImportRequest invalidRequest = request;
        invalidRequest.executionMode = ImportExecutionMode::Deferred;
        ImportResult invalid = createStatusResult(invalidRequest, statusImportFailed());
        if (request.libraryPath.empty()) {
            invalid.error = "Cartographer import requires a library path";
        } else if (request.headerPath.empty()) {
            invalid.error = "Cartographer import requires a header path";
        } else {
            invalid.error = "Cartographer import requires a scope name";
        }
        invalid.ok = false;
        invalid.definitions = createImportHandleValue(invalid, invalidRequest);
        return invalid;
    }

    return queueDeferredImport(request);
}

ImportResult CartographerService::importStatus(const std::string& requestId) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    CPtr<Impl::DeferredState> state = findTreeValue(impl->requests, requestId);
    if (!state) {
        ImportResult missing;
        missing.executionMode = ImportExecutionMode::Deferred;
        missing.requestId = requestId;
        missing.status = "missing";
        missing.error = "Unknown cartographer request id: " + requestId;
        return missing;
    }

    ImportRequest decodedRequest;
    ImportResult decodedResult;
    std::string decodeError;
    if (!protocol::decodeImportStatus(state->responseMessage, decodedRequest, decodedResult, nullptr, decodeError)) {
        ImportResult failed = createStatusResult(state->request,
                                                 statusTransportFailed(),
                                                 "Failed to decode Cartographer protocol status: " + decodeError,
                                                 state->symbolCount);
        failed.ok = false;
        return failed;
    }

    decodedResult.definitions = createImportHandleValue(decodedResult, decodedRequest);
    return decodedResult;
}

bool CartographerService::processDeferredRequest(const std::string& requestId, ImportResult& result) {
    Mapper::ParseDescription description;
    ImportRequest request;
    ImportResult statusResult;

    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        CPtr<Impl::DeferredState> state = findTreeValue(impl->requests, requestId);
        if (!state) {
            result = {};
            result.executionMode = ImportExecutionMode::Deferred;
            result.requestId = requestId;
            result.status = "missing";
            result.error = "Unknown cartographer request id: " + requestId;
            return false;
        }

        std::string decodeError;
        if (!protocol::decodeImportStatus(state->responseMessage, request, statusResult, nullptr, decodeError)) {
            result = createStatusResult(state->request,
                                        statusTransportFailed(),
                                        "Failed to decode Cartographer protocol status: " + decodeError,
                                        state->symbolCount);
            result.ok = false;
            return false;
        }
        if (!isPendingStatus(statusResult.status) && !isReadyStatus(statusResult.status)) {
            result = createStatusResult(request, statusResult.status, statusResult.error, statusResult.symbolCount);
            result.ok = false;
            return false;
        }
        if (!isReadyStatus(statusResult.status)) {
            result = createStatusResult(request, statusResult.status, "Cartographer deferred import not ready", statusResult.symbolCount);
            result.ok = false;
            return false;
        }

        description = state->description;
    }

    result = {};
    result.ok = true;
    result.executionMode = ImportExecutionMode::Deferred;
    result.requestId = request.requestId;
    result.scopeName = request.scopeName;
    result.status = statusReady();
    result.definitions = materializeImportedDefinitions(description, request, ImportExecutionMode::Deferred, result.symbolCount);
    if (!result.definitions) {
        result.ok = false;
        result.status = statusImportFailed();
        result.error = "Failed to materialize deferred cartographer import definitions";
        result.definitions = createImportHandleValue(result, request);
        return false;
    }
    return true;
}

ImportResult CartographerService::collect(const std::string& requestId) {
    ImportResult result;
    processDeferredRequest(requestId, result);
    return result;
}

} // namespace cartographer
} // namespace agentc
