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

#include "tcc_runtime.h"
#include "pipe_io.h"
#include "tcc_shared.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <spawn.h>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AGENTC_HAVE_TCC
#define AGENTC_HAVE_TCC 0
#endif

#ifndef AGENTC_TCC_WORKER_EXECUTABLE
#define AGENTC_TCC_WORKER_EXECUTABLE ""
#endif

extern char** environ;

namespace agentc::edict::tcc {
namespace {

using Clock = std::chrono::steady_clock;
constexpr const char* kExecLaunchMode = "exec";

struct StoredModule {
    std::string id;
    std::string source;
    std::string entrySymbol = "agentc_tcc_entry";
    std::vector<BoundSymbol> boundSymbols;
    std::vector<std::string> symbols;
    std::vector<std::string> diagnostics;
};

struct TccJob {
    std::string id;
    std::string moduleId;
    long long timeoutMs = 0;
    pid_t pid = -1;
    int readFd = -1;
    Clock::time_point startedAt = Clock::now();
    bool done = false;
    TccEnvelope cached;
};

// ── Wire protocol helpers ─────────────────────────────────────────────────────

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

bool readEnvelope(int fd, TccEnvelope& envelope) {
    uint8_t ok = 0;
    uint8_t available = 0;
    uint64_t symbolCount = 0;
    if (!readAll(fd, &ok, sizeof(ok)) ||
        !readAll(fd, &available, sizeof(available)) ||
        !readString(fd, envelope.status) ||
        !readString(fd, envelope.error) ||
        !readString(fd, envelope.moduleId) ||
        !readString(fd, envelope.jobId) ||
        !readString(fd, envelope.entrySymbol) ||
        !readString(fd, envelope.resultKind) ||
        !readString(fd, envelope.resultText) ||
        !readAll(fd, &envelope.resultI64, sizeof(envelope.resultI64)) ||
        !readAll(fd, &envelope.resultF64, sizeof(envelope.resultF64)) ||
        !readAll(fd, &envelope.signalNumber, sizeof(envelope.signalNumber)) ||
        !readAll(fd, &envelope.exitCode, sizeof(envelope.exitCode)) ||
        !readAll(fd, &envelope.pid, sizeof(envelope.pid)) ||
        !readAll(fd, &envelope.timeoutMs, sizeof(envelope.timeoutMs)) ||
        !readAll(fd, &symbolCount, sizeof(symbolCount)) ||
        !readString(fd, envelope.handleKind) ||
        !readString(fd, envelope.launchMode) ||
        !readStringVector(fd, envelope.diagnostics) ||
        !readStringVector(fd, envelope.symbols) ||
        !readStringVector(fd, envelope.logs)) {
        return false;
    }
    envelope.ok = ok != 0;
    envelope.available = available != 0;
    envelope.symbolCount = static_cast<std::size_t>(symbolCount);
    return true;
}

bool writeWorkerRequest(int fd,
                        const std::string& mode,
                        const std::string& source,
                        const std::vector<BoundSymbol>& symbols,
                        const std::vector<std::string>& args) {
    return writeString(fd, mode) &&
           writeString(fd, source) &&
           writeBoundSymbols(fd, symbols) &&
           writeStringVector(fd, args);
}

// ── Envelope factories ────────────────────────────────────────────────────────

std::string boolText(bool value) {
    return value ? "true" : "false";
}

std::string defaultWorkerExecutablePath() {
    const char* fromEnv = std::getenv("AGENTC_TCC_WORKER_EXEC");
    if (fromEnv && *fromEnv) {
        return fromEnv;
    }
    return AGENTC_TCC_WORKER_EXECUTABLE;
}

TccEnvelope unavailableEnvelope(const std::string& reason) {
    TccEnvelope envelope;
    envelope.available = false;
    envelope.ok = false;
    envelope.status = "tcc_unavailable";
    envelope.error = reason;
    envelope.handleKind = "tcc_status";
    return envelope;
}

TccEnvelope moduleNotFoundEnvelope(const std::string& moduleId) {
    TccEnvelope envelope;
    envelope.available = true;
    envelope.ok = false;
    envelope.status = "module_not_found";
    envelope.error = "Unknown TCC module: " + moduleId;
    envelope.moduleId = moduleId;
    envelope.handleKind = "tcc_module";
    return envelope;
}

TccEnvelope jobNotFoundEnvelope(const std::string& jobId) {
    TccEnvelope envelope;
    envelope.available = true;
    envelope.ok = false;
    envelope.status = "job_not_found";
    envelope.error = "Unknown TCC job: " + jobId;
    envelope.jobId = jobId;
    envelope.handleKind = "tcc_job";
    return envelope;
}

/// Build a failed-job envelope, stamping all standard job identity fields.
TccEnvelope jobErrorEnvelope(const TccJob& job,
                              const std::string& status,
                              const std::string& error) {
    TccEnvelope envelope;
    envelope.available = true;
    envelope.ok = false;
    envelope.status = status;
    envelope.error = error;
    envelope.jobId = job.id;
    envelope.moduleId = job.moduleId;
    envelope.timeoutMs = job.timeoutMs;
    envelope.handleKind = "tcc_job";
    envelope.launchMode = kExecLaunchMode;
    return envelope;
}

std::string workerAvailabilityError(bool built,
                                    const std::string& workerExecutablePath) {
    if (!built) {
        return "AgentC built without libtcc support";
    }
    if (workerExecutablePath.empty()) {
        return "TCC worker executable path is empty";
    }
    if (::access(workerExecutablePath.c_str(), X_OK) != 0) {
        return std::string("TCC worker executable not available: ") +
               workerExecutablePath;
    }
    return {};
}

// ── Worker launch / job lifecycle ─────────────────────────────────────────────

bool launchWorker(const std::string& executablePath,
                  const std::string& mode,
                  const std::string& source,
                  const std::vector<BoundSymbol>& symbols,
                  const std::vector<std::string>& args,
                  TccJob* asyncJob,
                  TccEnvelope* immediateFailure) {
    int inputPipe[2] = {-1, -1};
    int outputPipe[2] = {-1, -1};
    if (::pipe(inputPipe) != 0) {
        if (immediateFailure) {
            *immediateFailure = unavailableEnvelope(
                std::string("input pipe failed: ") + std::strerror(errno));
            immediateFailure->available = true;
            immediateFailure->status = "spawn_failed";
        }
        return false;
    }
    if (::pipe(outputPipe) != 0) {
        if (immediateFailure) {
            *immediateFailure = unavailableEnvelope(
                std::string("output pipe failed: ") + std::strerror(errno));
            immediateFailure->available = true;
            immediateFailure->status = "spawn_failed";
        }
        ::close(inputPipe[0]);
        ::close(inputPipe[1]);
        return false;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, inputPipe[1]);
    posix_spawn_file_actions_addclose(&actions, outputPipe[0]);

    const std::string inputFdText = std::to_string(inputPipe[0]);
    const std::string outputFdText = std::to_string(outputPipe[1]);
    char* const argv[] = {
        const_cast<char*>(executablePath.c_str()),
        const_cast<char*>(inputFdText.c_str()),
        const_cast<char*>(outputFdText.c_str()),
        nullptr,
    };

    pid_t pid = -1;
    const int spawnRc = ::posix_spawn(&pid,
                                      executablePath.c_str(),
                                      &actions,
                                      nullptr,
                                      argv,
                                      environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawnRc != 0) {
        if (immediateFailure) {
            *immediateFailure = unavailableEnvelope(
                std::string("posix_spawn failed: ") + std::strerror(spawnRc));
            immediateFailure->available = true;
            immediateFailure->status = "spawn_failed";
        }
        ::close(inputPipe[0]);
        ::close(inputPipe[1]);
        ::close(outputPipe[0]);
        ::close(outputPipe[1]);
        return false;
    }

    ::close(inputPipe[0]);
    ::close(outputPipe[1]);
    const bool wrote = writeWorkerRequest(inputPipe[1], mode, source, symbols, args);
    ::close(inputPipe[1]);
    if (!wrote) {
        if (immediateFailure) {
            *immediateFailure = unavailableEnvelope("failed to write TCC worker request");
            immediateFailure->available = true;
            immediateFailure->status = "spawn_failed";
        }
        ::close(outputPipe[0]);
        ::kill(pid, SIGKILL);
        (void)::waitpid(pid, nullptr, 0);
        return false;
    }

    if (asyncJob) {
        asyncJob->pid = pid;
        asyncJob->readFd = outputPipe[0];
        asyncJob->startedAt = Clock::now();
        return true;
    }

    TccEnvelope envelope;
    const bool readOk = readEnvelope(outputPipe[0], envelope);
    ::close(outputPipe[0]);

    int waitStatus = 0;
    (void)::waitpid(pid, &waitStatus, 0);
    if (!readOk) {
        if (immediateFailure) {
            *immediateFailure = unavailableEnvelope(
                "TCC worker exited without returning a complete envelope");
            immediateFailure->available = true;
            immediateFailure->status = "worker_exit_without_result";
        }
        return false;
    }
    if (immediateFailure) {
        *immediateFailure = std::move(envelope);
    }
    return true;
}

void finalizeJob(TccJob& job, int waitStatus) {
    job.done = true;
    if (job.readFd >= 0) {
        if (!readEnvelope(job.readFd, job.cached)) {
            job.cached = jobErrorEnvelope(
                job,
                WIFSIGNALED(waitStatus) ? "runtime_signal"
                                        : "worker_exit_without_result",
                WIFSIGNALED(waitStatus)
                    ? ("TCC worker died from signal " +
                       std::to_string(WTERMSIG(waitStatus)))
                    : "TCC worker exited without producing a result envelope");
            job.cached.signalNumber = WIFSIGNALED(waitStatus)
                ? WTERMSIG(waitStatus)
                : 0;
            job.cached.exitCode = WIFEXITED(waitStatus)
                ? WEXITSTATUS(waitStatus)
                : waitStatus;
        }
        ::close(job.readFd);
        job.readFd = -1;
    }
    job.cached.jobId = job.id;
    job.cached.moduleId = job.moduleId;
    job.cached.timeoutMs = job.timeoutMs;
    job.cached.handleKind = "tcc_job";
    job.cached.launchMode = kExecLaunchMode;
}

long long elapsedMs(const TccJob& job) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - job.startedAt).count();
}

TccEnvelope jobEnvelope(const TccJob& job, const TccEnvelope& base) {
    auto envelope = base;
    envelope.jobId = job.id;
    envelope.moduleId = job.moduleId;
    envelope.timeoutMs = job.timeoutMs;
    envelope.handleKind = "tcc_job";
    envelope.launchMode = kExecLaunchMode;
    return envelope;
}

} // namespace

struct TccCompilerService::Impl {
    mutable std::mutex mutex;
    bool available = AGENTC_HAVE_TCC != 0;
    std::string unavailableReason = AGENTC_HAVE_TCC
        ? ""
        : "AgentC built without libtcc support";
    std::string workerExecutablePath = defaultWorkerExecutablePath();
    int nextModuleId = 1;
    int nextJobId = 1;
    std::unordered_map<std::string, StoredModule> modules;
    std::unordered_map<std::string, std::unique_ptr<TccJob>> jobs;
    std::unordered_map<std::string, BoundSymbol> allowedSymbols;
    std::unordered_map<std::string, void*> validationHandles;
    void* processHandle = nullptr;

    // Shared implementation for allowProcessSymbol / allowLibrarySymbol.
    TccEnvelope allowSymbol(const std::string& name,
                            const std::string& declaration,
                            const std::string& origin) {
        BoundSymbol symbol{name, declaration, origin};
        std::string error;
        if (!resolveSymbolOrigin(symbol, validationHandles,
                                 processHandle, nullptr, error)) {
            TccEnvelope envelope;
            envelope.available = true;
            envelope.ok = false;
            envelope.status = "symbol_not_found";
            envelope.error = error;
            return envelope;
        }

        allowedSymbols[name] = std::move(symbol);
        TccEnvelope envelope;
        envelope.available = true;
        envelope.ok = true;
        envelope.status = "allowed";
        envelope.entrySymbol = name;
        envelope.handleKind = "tcc_symbol";
        envelope.resultText = origin;
        return envelope;
    }
};

TccCompilerService::TccCompilerService()
    : impl_(std::make_unique<Impl>()) {}

TccCompilerService::~TccCompilerService() {
    if (!impl_) {
        return;
    }
    for (auto& [id, job] : impl_->jobs) {
        (void)id;
        if (!job || job->done || job->pid <= 0) {
            continue;
        }
        ::kill(job->pid, SIGKILL);
        (void)::waitpid(job->pid, nullptr, 0);
        if (job->readFd >= 0) {
            ::close(job->readFd);
            job->readFd = -1;
        }
    }
    closeLibraryHandles(impl_->validationHandles);
}

TccEnvelope TccCompilerService::availability() const {
    const std::string error = workerAvailabilityError(
        impl_ && impl_->available,
        impl_ ? impl_->workerExecutablePath : std::string());
    TccEnvelope envelope;
    envelope.available = error.empty();
    envelope.ok = envelope.available;
    envelope.status = envelope.available ? "available" : "tcc_unavailable";
    envelope.error = error;
    envelope.handleKind = "tcc_status";
    envelope.resultText = boolText(envelope.available);
    return envelope;
}

TccEnvelope TccCompilerService::compile(const std::string& source) {
    const std::string workerError = workerAvailabilityError(
        impl_->available, impl_->workerExecutablePath);
    if (!workerError.empty()) {
        return unavailableEnvelope(workerError);
    }

    std::vector<BoundSymbol> symbols;
    std::string moduleId;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        moduleId = "tcc_module_" + std::to_string(impl_->nextModuleId++);
        symbols.reserve(impl_->allowedSymbols.size());
        for (const auto& [name, symbol] : impl_->allowedSymbols) {
            (void)name;
            symbols.push_back(symbol);
        }
    }

    TccEnvelope envelope;
    if (!launchWorker(impl_->workerExecutablePath, kModeCompile, source, symbols,
                      {}, nullptr, &envelope)) {
        return envelope;
    }
    if (!envelope.ok) {
        return envelope;
    }

    StoredModule module;
    module.id = moduleId;
    module.source = source;
    module.boundSymbols = symbols;
    module.symbols = envelope.symbols;
    module.diagnostics = envelope.diagnostics;

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->modules[moduleId] = std::move(module);
    }
    envelope.moduleId = moduleId;
    envelope.handleKind = "tcc_module";
    return envelope;
}

TccEnvelope TccCompilerService::run(const std::string& moduleId,
                                    const std::vector<std::string>& args) {
    const std::string workerError = workerAvailabilityError(
        impl_->available, impl_->workerExecutablePath);
    if (!workerError.empty()) {
        return unavailableEnvelope(workerError);
    }

    StoredModule module;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto it = impl_->modules.find(moduleId);
        if (it == impl_->modules.end()) {
            return moduleNotFoundEnvelope(moduleId);
        }
        module = it->second;
    }

    TccEnvelope envelope;
    if (!launchWorker(impl_->workerExecutablePath, kModeRun, module.source,
                      module.boundSymbols, args, nullptr, &envelope)) {
        return envelope;
    }
    envelope.moduleId = moduleId;
    envelope.entrySymbol = module.entrySymbol;
    envelope.handleKind = "tcc_module";
    return envelope;
}

TccEnvelope TccCompilerService::listSymbols(const std::string& moduleId) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->modules.find(moduleId);
    if (it == impl_->modules.end()) {
        return moduleNotFoundEnvelope(moduleId);
    }
    TccEnvelope envelope;
    envelope.available = true;
    envelope.ok = true;
    envelope.status = "symbols";
    envelope.moduleId = moduleId;
    envelope.symbols = it->second.symbols;
    envelope.symbolCount = it->second.symbols.size();
    envelope.handleKind = "tcc_module";
    return envelope;
}

TccEnvelope TccCompilerService::drop(const std::string& moduleId) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->modules.find(moduleId);
    if (it == impl_->modules.end()) {
        return moduleNotFoundEnvelope(moduleId);
    }
    impl_->modules.erase(it);
    TccEnvelope envelope;
    envelope.available = true;
    envelope.ok = true;
    envelope.status = "dropped";
    envelope.moduleId = moduleId;
    envelope.handleKind = "tcc_module";
    return envelope;
}

TccEnvelope TccCompilerService::startIsolated(const std::string& moduleId,
                                              const std::vector<std::string>& args,
                                              long long timeoutMs) {
    const std::string workerError = workerAvailabilityError(
        impl_->available, impl_->workerExecutablePath);
    if (!workerError.empty()) {
        return unavailableEnvelope(workerError);
    }

    StoredModule module;
    auto job = std::make_unique<TccJob>();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto it = impl_->modules.find(moduleId);
        if (it == impl_->modules.end()) {
            return moduleNotFoundEnvelope(moduleId);
        }
        module = it->second;
        job->id = "tcc_job_" + std::to_string(impl_->nextJobId++);
    }

    job->moduleId = moduleId;
    job->timeoutMs = timeoutMs;
    TccEnvelope failure;
    if (!launchWorker(impl_->workerExecutablePath, kModeRun, module.source,
                      module.boundSymbols, args, job.get(), &failure)) {
        failure.moduleId = moduleId;
        failure.handleKind = "tcc_job";
        failure.launchMode = kExecLaunchMode;
        return failure;
    }

    TccEnvelope envelope;
    envelope.available = true;
    envelope.ok = true;
    envelope.status = "running";
    envelope.jobId = job->id;
    envelope.moduleId = moduleId;
    envelope.pid = job->pid;
    envelope.timeoutMs = timeoutMs;
    envelope.handleKind = "tcc_job";
    envelope.launchMode = kExecLaunchMode;

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->jobs[job->id] = std::move(job);
    return envelope;
}

TccEnvelope TccCompilerService::status(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->jobs.find(jobId);
    if (it == impl_->jobs.end()) {
        return jobNotFoundEnvelope(jobId);
    }

    auto& job = *it->second;
    if (!job.done) {
        int waitStatus = 0;
        const pid_t waited = ::waitpid(job.pid, &waitStatus, WNOHANG);
        if (waited == job.pid) {
            finalizeJob(job, waitStatus);
        } else if (waited == 0 && job.timeoutMs > 0 &&
                   elapsedMs(job) > job.timeoutMs) {
            ::kill(job.pid, SIGKILL);
            (void)::waitpid(job.pid, &waitStatus, 0);
            if (job.readFd >= 0) {
                ::close(job.readFd);
                job.readFd = -1;
            }
            job.done = true;
            job.cached = jobErrorEnvelope(job, "timed_out",
                                          "TCC worker exceeded timeout");
        }
    }

    if (job.done) {
        return jobEnvelope(job, job.cached);
    }

    TccEnvelope envelope;
    envelope.available = true;
    envelope.ok = true;
    envelope.status = "running";
    envelope.jobId = job.id;
    envelope.moduleId = job.moduleId;
    envelope.pid = job.pid;
    envelope.timeoutMs = job.timeoutMs;
    envelope.handleKind = "tcc_job";
    envelope.launchMode = kExecLaunchMode;
    return envelope;
}

TccEnvelope TccCompilerService::collect(const std::string& jobId) {
    while (true) {
        auto envelope = status(jobId);
        if (envelope.status != "running") {
            return envelope;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

TccEnvelope TccCompilerService::cancel(const std::string& jobId) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->jobs.find(jobId);
    if (it == impl_->jobs.end()) {
        return jobNotFoundEnvelope(jobId);
    }

    auto& job = *it->second;
    if (job.done) {
        return jobEnvelope(job, job.cached);
    }

    ::kill(job.pid, SIGKILL);
    (void)::waitpid(job.pid, nullptr, 0);
    if (job.readFd >= 0) {
        ::close(job.readFd);
        job.readFd = -1;
    }
    job.done = true;
    job.cached = jobErrorEnvelope(job, "cancelled",
                                  "TCC worker cancelled by coordinator");
    return job.cached;
}

TccEnvelope TccCompilerService::allowProcessSymbol(const std::string& name,
                                                   const std::string& declaration) {
    return impl_->allowSymbol(name, declaration, kProcessOrigin);
}

TccEnvelope TccCompilerService::allowLibrarySymbol(const std::string& libraryPath,
                                                   const std::string& name,
                                                   const std::string& declaration) {
    return impl_->allowSymbol(name, declaration, libraryPath);
}

TccEnvelope TccCompilerService::clearAllowedSymbols() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->allowedSymbols.clear();
    TccEnvelope envelope;
    envelope.available = true;
    envelope.ok = true;
    envelope.status = "cleared";
    envelope.handleKind = "tcc_symbol_cache";
    return envelope;
}

} // namespace agentc::edict::tcc
