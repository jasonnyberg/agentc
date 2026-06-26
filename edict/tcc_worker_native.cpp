// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include "tcc_runtime.h"
#include "agentc_tcc_api.h"

#include <cerrno>
#include <cstdint>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unistd.h>

#include <libtcc.h>

#ifndef AGENTC_TCC_API_INCLUDE_DIR
#define AGENTC_TCC_API_INCLUDE_DIR "."
#endif

#ifndef AGENTC_TCC_LIBRARY_ROOT
#define AGENTC_TCC_LIBRARY_ROOT ""
#endif

struct agentc_tcc_call {
    const std::vector<std::string>* args = nullptr;
    std::string resultText;
    long long resultI64 = 0;
    double resultF64 = 0.0;
    enum class ResultKind {
        None,
        Text,
        I64,
        F64,
    } resultKind = ResultKind::None;
    std::vector<std::string> logs;
};

extern "C" {

int agentc_tcc_arg_count(agentc_tcc_call* call) {
    return call && call->args ? static_cast<int>(call->args->size()) : 0;
}

const char* agentc_tcc_arg_text(agentc_tcc_call* call, int index) {
    if (!call || !call->args || index < 0 ||
        static_cast<std::size_t>(index) >= call->args->size()) {
        return "";
    }
    return (*call->args)[static_cast<std::size_t>(index)].c_str();
}

long long agentc_tcc_parse_i64(const char* text) {
    if (!text || !*text) {
        return 0;
    }
    char* end = nullptr;
    const long long value = std::strtoll(text, &end, 10);
    return (end && *end == '\0') ? value : 0;
}

double agentc_tcc_parse_f64(const char* text) {
    if (!text || !*text) {
        return 0.0;
    }
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    return (end && *end == '\0') ? value : 0.0;
}

void agentc_tcc_result_text(agentc_tcc_call* call, const char* text) {
    if (!call) {
        return;
    }
    call->resultText = text ? text : "";
    call->resultKind = agentc_tcc_call::ResultKind::Text;
}

void agentc_tcc_result_i64(agentc_tcc_call* call, long long value) {
    if (!call) {
        return;
    }
    call->resultI64 = value;
    call->resultKind = agentc_tcc_call::ResultKind::I64;
}

void agentc_tcc_result_f64(agentc_tcc_call* call, double value) {
    if (!call) {
        return;
    }
    call->resultF64 = value;
    call->resultKind = agentc_tcc_call::ResultKind::F64;
}

void agentc_tcc_log(agentc_tcc_call* call, const char* message) {
    if (!call) {
        return;
    }
    call->logs.emplace_back(message ? message : "");
}

} // extern "C"

namespace agentc::edict::tcc {
namespace {

constexpr const char* kProcessOrigin = "<process>";
constexpr const char* kModeCompile = "compile";
constexpr const char* kModeRun = "run";

struct BoundSymbol {
    std::string name;
    std::string declaration;
    std::string origin;
    const void* address = nullptr;
};

using AgentcTccEntry = int (*)(agentc_tcc_call*);

struct TccModule {
    std::string entrySymbol = "agentc_tcc_entry";
    std::vector<BoundSymbol> boundSymbols;
    std::vector<std::string> diagnostics;
    std::vector<std::string> symbols;
    TCCState* state = nullptr;
    AgentcTccEntry entry = nullptr;

    ~TccModule() {
        if (state) {
            tcc_delete(state);
            state = nullptr;
        }
    }
};

struct ErrorSink {
    std::vector<std::string>* diagnostics = nullptr;
};

void tccErrorCallback(void* opaque, const char* message) {
    auto* sink = static_cast<ErrorSink*>(opaque);
    if (sink && sink->diagnostics) {
        sink->diagnostics->push_back(message ? message : "libtcc error");
    }
}

bool writeAll(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t remaining = size;
    while (remaining > 0) {
        const ssize_t written = ::write(fd, bytes, remaining);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        bytes += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

bool readAll(int fd, void* data, std::size_t size) {
    auto* bytes = static_cast<char*>(data);
    std::size_t remaining = size;
    while (remaining > 0) {
        const ssize_t count = ::read(fd, bytes, remaining);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        bytes += count;
        remaining -= static_cast<std::size_t>(count);
    }
    return true;
}

bool writeString(int fd, const std::string& value) {
    const uint64_t length = static_cast<uint64_t>(value.size());
    return writeAll(fd, &length, sizeof(length)) &&
           (value.empty() || writeAll(fd, value.data(), value.size()));
}

bool readString(int fd, std::string& value) {
    uint64_t length = 0;
    if (!readAll(fd, &length, sizeof(length))) {
        return false;
    }
    value.assign(static_cast<std::size_t>(length), '\0');
    return length == 0 || readAll(fd, value.data(), value.size());
}

bool writeStringVector(int fd, const std::vector<std::string>& values) {
    const uint64_t count = static_cast<uint64_t>(values.size());
    if (!writeAll(fd, &count, sizeof(count))) {
        return false;
    }
    for (const auto& value : values) {
        if (!writeString(fd, value)) {
            return false;
        }
    }
    return true;
}

bool readStringVector(int fd, std::vector<std::string>& values) {
    uint64_t count = 0;
    if (!readAll(fd, &count, sizeof(count))) {
        return false;
    }
    values.clear();
    values.reserve(static_cast<std::size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        std::string value;
        if (!readString(fd, value)) {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

bool writeBoundSymbols(int fd, const std::vector<BoundSymbol>& symbols) {
    const uint64_t count = static_cast<uint64_t>(symbols.size());
    if (!writeAll(fd, &count, sizeof(count))) {
        return false;
    }
    for (const auto& symbol : symbols) {
        if (!writeString(fd, symbol.name) ||
            !writeString(fd, symbol.declaration) ||
            !writeString(fd, symbol.origin)) {
            return false;
        }
    }
    return true;
}

bool readBoundSymbols(int fd, std::vector<BoundSymbol>& symbols) {
    uint64_t count = 0;
    if (!readAll(fd, &count, sizeof(count))) {
        return false;
    }
    symbols.clear();
    symbols.reserve(static_cast<std::size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        BoundSymbol symbol;
        if (!readString(fd, symbol.name) ||
            !readString(fd, symbol.declaration) ||
            !readString(fd, symbol.origin)) {
            return false;
        }
        symbols.push_back(std::move(symbol));
    }
    return true;
}

bool writeEnvelope(int fd, const TccEnvelope& envelope) {
    const uint8_t ok = envelope.ok ? 1 : 0;
    const uint8_t available = envelope.available ? 1 : 0;
    const uint64_t symbolCount = static_cast<uint64_t>(envelope.symbolCount);
    return writeAll(fd, &ok, sizeof(ok)) &&
           writeAll(fd, &available, sizeof(available)) &&
           writeString(fd, envelope.status) &&
           writeString(fd, envelope.error) &&
           writeString(fd, envelope.moduleId) &&
           writeString(fd, envelope.jobId) &&
           writeString(fd, envelope.entrySymbol) &&
           writeString(fd, envelope.resultKind) &&
           writeString(fd, envelope.resultText) &&
           writeAll(fd, &envelope.resultI64, sizeof(envelope.resultI64)) &&
           writeAll(fd, &envelope.resultF64, sizeof(envelope.resultF64)) &&
           writeAll(fd, &envelope.signalNumber, sizeof(envelope.signalNumber)) &&
           writeAll(fd, &envelope.exitCode, sizeof(envelope.exitCode)) &&
           writeAll(fd, &envelope.pid, sizeof(envelope.pid)) &&
           writeAll(fd, &envelope.timeoutMs, sizeof(envelope.timeoutMs)) &&
           writeAll(fd, &symbolCount, sizeof(symbolCount)) &&
           writeString(fd, envelope.handleKind) &&
           writeString(fd, envelope.launchMode) &&
           writeStringVector(fd, envelope.diagnostics) &&
           writeStringVector(fd, envelope.symbols) &&
           writeStringVector(fd, envelope.logs);
}

bool readRequest(int fd,
                 std::string& mode,
                 std::string& source,
                 std::vector<BoundSymbol>& symbols,
                 std::vector<std::string>& args) {
    return readString(fd, mode) &&
           readString(fd, source) &&
           readBoundSymbols(fd, symbols) &&
           readStringVector(fd, args);
}

bool parseFdText(const char* text, int& fd) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);
    if (errno != 0 || end == text || (end && *end != '\0') ||
        parsed < 0 || parsed > INT_MAX) {
        return false;
    }
    fd = static_cast<int>(parsed);
    return true;
}

std::string joinDiagnostics(const std::vector<std::string>& diagnostics) {
    std::ostringstream out;
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i) {
            out << '\n';
        }
        out << diagnostics[i];
    }
    return out.str();
}

void closeDynamicLibraryHandles(const std::unordered_map<std::string, void*>& handles) {
    for (const auto& [origin, handle] : handles) {
        if (handle && origin != kProcessOrigin) {
            ::dlclose(handle);
        }
    }
}

bool resolveSymbolSpecInCurrentProcess(const BoundSymbol& spec,
                                       std::unordered_map<std::string, void*>& libraryHandles,
                                       void*& processHandle,
                                       BoundSymbol& resolved,
                                       std::string& error) {
    resolved = spec;
    if (resolved.origin.empty()) {
        error = "symbol '" + resolved.name + "' has no origin";
        return false;
    }

    void* handle = nullptr;
    if (resolved.origin == kProcessOrigin) {
        if (!processHandle) {
            processHandle = ::dlopen(nullptr, RTLD_NOW);
            if (processHandle) {
                libraryHandles[kProcessOrigin] = processHandle;
            }
        }
        handle = processHandle;
        if (!handle) {
            error = ::dlerror() ? ::dlerror() : "dlopen(nullptr) failed";
            return false;
        }
    } else {
        auto existing = libraryHandles.find(resolved.origin);
        if (existing != libraryHandles.end()) {
            handle = existing->second;
        } else {
            handle = ::dlopen(resolved.origin.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (!handle) {
                error = ::dlerror() ? ::dlerror()
                                    : ("Failed to open library: " + resolved.origin);
                return false;
            }
            libraryHandles[resolved.origin] = handle;
        }
    }

    ::dlerror();
    resolved.address = ::dlsym(handle, resolved.name.c_str());
    const char* symError = ::dlerror();
    if (!resolved.address || symError) {
        error = symError ? symError
                         : ("Failed to resolve symbol '" + resolved.name + "'");
        resolved.address = nullptr;
        return false;
    }
    return true;
}

std::string buildPrelude(const std::vector<BoundSymbol>& symbols) {
    std::ostringstream out;
    out << "#include \"agentc_tcc_api.h\"\n";
    for (const auto& symbol : symbols) {
        if (!symbol.declaration.empty()) {
            out << symbol.declaration << '\n';
        }
    }
    out << "#line 1 \"agentc_tcc_user_source.c\"\n";
    return out.str();
}

void registerHelperSymbols(TCCState* state) {
    tcc_add_symbol(state, "agentc_tcc_arg_count",
                   reinterpret_cast<const void*>(&agentc_tcc_arg_count));
    tcc_add_symbol(state, "agentc_tcc_arg_text",
                   reinterpret_cast<const void*>(&agentc_tcc_arg_text));
    tcc_add_symbol(state, "agentc_tcc_parse_i64",
                   reinterpret_cast<const void*>(&agentc_tcc_parse_i64));
    tcc_add_symbol(state, "agentc_tcc_parse_f64",
                   reinterpret_cast<const void*>(&agentc_tcc_parse_f64));
    tcc_add_symbol(state, "agentc_tcc_result_text",
                   reinterpret_cast<const void*>(&agentc_tcc_result_text));
    tcc_add_symbol(state, "agentc_tcc_result_i64",
                   reinterpret_cast<const void*>(&agentc_tcc_result_i64));
    tcc_add_symbol(state, "agentc_tcc_result_f64",
                   reinterpret_cast<const void*>(&agentc_tcc_result_f64));
    tcc_add_symbol(state, "agentc_tcc_log",
                   reinterpret_cast<const void*>(&agentc_tcc_log));
}

void listCompiledSymbols(TCCState* state, std::vector<std::string>& out) {
    out.clear();
    tcc_list_symbols(state, &out,
        [](void* ctx, const char* name, const void*) {
            auto* target = static_cast<std::vector<std::string>*>(ctx);
            target->push_back(name ? name : "");
        });
}

std::unique_ptr<TccModule> compileModule(const std::string& source,
                                         const std::vector<BoundSymbol>& symbolSpecs,
                                         TccEnvelope& envelope) {
    envelope = {};
    envelope.available = true;
    envelope.entrySymbol = "agentc_tcc_entry";
    envelope.handleKind = "tcc_module";

    auto module = std::make_unique<TccModule>();
    module->boundSymbols = symbolSpecs;

    std::unordered_map<std::string, void*> localHandles;
    void* processHandle = nullptr;
    std::vector<BoundSymbol> resolvedSymbols;
    resolvedSymbols.reserve(symbolSpecs.size());
    for (const auto& spec : symbolSpecs) {
        BoundSymbol resolved;
        std::string error;
        if (!resolveSymbolSpecInCurrentProcess(spec, localHandles,
                                               processHandle, resolved, error)) {
            closeDynamicLibraryHandles(localHandles);
            envelope.ok = false;
            envelope.status = "symbol_resolution_failed";
            envelope.error = error;
            return nullptr;
        }
        resolvedSymbols.push_back(std::move(resolved));
    }

    ErrorSink sink{&module->diagnostics};
    module->state = tcc_new();
    if (!module->state) {
        closeDynamicLibraryHandles(localHandles);
        envelope.ok = false;
        envelope.status = "tcc_new_failed";
        envelope.error = "Failed to create libtcc state";
        return nullptr;
    }

    tcc_set_error_func(module->state, &sink, &tccErrorCallback);
    if (std::strlen(AGENTC_TCC_LIBRARY_ROOT) > 0) {
        tcc_set_lib_path(module->state, AGENTC_TCC_LIBRARY_ROOT);
        tcc_add_library_path(module->state, AGENTC_TCC_LIBRARY_ROOT);
    }
    if (tcc_set_output_type(module->state, TCC_OUTPUT_MEMORY) < 0) {
        closeDynamicLibraryHandles(localHandles);
        envelope.ok = false;
        envelope.status = "output_type_failed";
        envelope.error = module->diagnostics.empty()
            ? "Failed to configure libtcc for in-memory output"
            : joinDiagnostics(module->diagnostics);
        return nullptr;
    }
    if (tcc_add_include_path(module->state, AGENTC_TCC_API_INCLUDE_DIR) < 0) {
        closeDynamicLibraryHandles(localHandles);
        module->diagnostics.push_back(
            std::string("Failed to add TCC include path: ") + AGENTC_TCC_API_INCLUDE_DIR);
        envelope.ok = false;
        envelope.status = "include_path_failed";
        envelope.error = joinDiagnostics(module->diagnostics);
        envelope.diagnostics = module->diagnostics;
        return nullptr;
    }

    registerHelperSymbols(module->state);
    for (const auto& symbol : resolvedSymbols) {
        if (!symbol.address ||
            tcc_add_symbol(module->state, symbol.name.c_str(), symbol.address) < 0) {
            closeDynamicLibraryHandles(localHandles);
            module->diagnostics.push_back("Failed to register host symbol: " + symbol.name);
            envelope.ok = false;
            envelope.status = "symbol_registration_failed";
            envelope.error = joinDiagnostics(module->diagnostics);
            envelope.diagnostics = module->diagnostics;
            return nullptr;
        }
    }

    const std::string payload = buildPrelude(symbolSpecs) + source;
    if (tcc_compile_string(module->state, payload.c_str()) < 0) {
        closeDynamicLibraryHandles(localHandles);
        envelope.ok = false;
        envelope.status = "compile_error";
        envelope.error = joinDiagnostics(module->diagnostics);
        envelope.diagnostics = module->diagnostics;
        return nullptr;
    }
    if (tcc_relocate(module->state) < 0) {
        closeDynamicLibraryHandles(localHandles);
        envelope.ok = false;
        envelope.status = "relocate_error";
        envelope.error = joinDiagnostics(module->diagnostics);
        envelope.diagnostics = module->diagnostics;
        return nullptr;
    }

    module->entry = reinterpret_cast<AgentcTccEntry>(
        tcc_get_symbol(module->state, module->entrySymbol.c_str()));
    if (!module->entry) {
        closeDynamicLibraryHandles(localHandles);
        envelope.ok = false;
        envelope.status = "missing_entry_symbol";
        envelope.error = "Compiled module did not export agentc_tcc_entry";
        listCompiledSymbols(module->state, module->symbols);
        envelope.symbols = module->symbols;
        return nullptr;
    }

    listCompiledSymbols(module->state, module->symbols);
    envelope.ok = true;
    envelope.status = "compiled";
    envelope.diagnostics = module->diagnostics;
    envelope.symbols = module->symbols;
    envelope.symbolCount = module->symbols.size();
    return module;
}

TccEnvelope runModule(const TccModule& module,
                      const std::vector<std::string>& args) {
    agentc_tcc_call call;
    call.args = &args;
    const int returnCode = module.entry ? module.entry(&call) : -1;

    TccEnvelope envelope;
    envelope.available = true;
    envelope.entrySymbol = module.entrySymbol;
    envelope.handleKind = "tcc_isolated_result";
    envelope.logs = call.logs;
    if (returnCode != 0) {
        envelope.ok = false;
        envelope.status = "entry_error";
        envelope.error = "agentc_tcc_entry returned non-zero status " +
                         std::to_string(returnCode);
        envelope.exitCode = returnCode;
        return envelope;
    }

    envelope.ok = true;
    envelope.status = "ok";
    switch (call.resultKind) {
    case agentc_tcc_call::ResultKind::Text:
        envelope.resultKind = "text";
        envelope.resultText = call.resultText;
        break;
    case agentc_tcc_call::ResultKind::I64:
        envelope.resultKind = "i64";
        envelope.resultI64 = call.resultI64;
        break;
    case agentc_tcc_call::ResultKind::F64:
        envelope.resultKind = "f64";
        envelope.resultF64 = call.resultF64;
        break;
    case agentc_tcc_call::ResultKind::None:
    default:
        envelope.resultKind = "none";
        break;
    }
    return envelope;
}

} // namespace

int runTccWorkerExecChildMain(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }

    int inputFd = -1;
    int outputFd = -1;
    if (!parseFdText(argv[1], inputFd) || !parseFdText(argv[2], outputFd)) {
        return 2;
    }

    std::string mode;
    std::string source;
    std::vector<BoundSymbol> symbols;
    std::vector<std::string> args;
    TccEnvelope envelope;
    if (!readRequest(inputFd, mode, source, symbols, args)) {
        envelope.available = true;
        envelope.ok = false;
        envelope.status = "worker_input_error";
        envelope.error = "TCC worker did not receive a complete request";
    } else if (mode == kModeCompile) {
        auto module = compileModule(source, symbols, envelope);
        (void)module;
    } else if (mode == kModeRun) {
        auto module = compileModule(source, symbols, envelope);
        if (module && envelope.ok) {
            envelope = runModule(*module, args);
        }
    } else {
        envelope.available = true;
        envelope.ok = false;
        envelope.status = "worker_input_error";
        envelope.error = "Unknown TCC worker mode: " + mode;
    }
    ::close(inputFd);
    envelope.launchMode = "exec";
    const bool wrote = writeEnvelope(outputFd, envelope);
    ::close(outputFd);
    return wrote ? 0 : 1;
}

} // namespace agentc::edict::tcc
