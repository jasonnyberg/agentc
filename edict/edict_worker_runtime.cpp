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

#include "edict_worker_runtime.h"
#include "edict_vm.h"
#include "edict_compiler.h"

#include <cerrno>
#include <exception>
#include <cstdint>
#include <cstring>
#include <sys/wait.h>
#include <mutex>
#include <unistd.h>
#include <utility>

namespace agentc::edict::worker {
namespace {

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value,
                                      const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

bool writeAll(int fd, const void* data, size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    while (size > 0) {
        const ssize_t written = ::write(fd, bytes, size);
        if (written <= 0) {
            return false;
        }
        bytes += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

bool readAll(int fd, void* data, size_t size) {
    auto* bytes = static_cast<char*>(data);
    while (size > 0) {
        const ssize_t count = ::read(fd, bytes, size);
        if (count <= 0) {
            return false;
        }
        bytes += count;
        size -= static_cast<size_t>(count);
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
    value.assign(static_cast<size_t>(length), '\0');
    return length == 0 || readAll(fd, value.data(), value.size());
}

bool writeOutcome(int fd, const InternWorkerOutcome& outcome) {
    const uint8_t ok = outcome.ok ? 1 : 0;
    const int32_t vmState = static_cast<int32_t>(outcome.vmState);
    return writeAll(fd, &ok, sizeof(ok)) &&
           writeAll(fd, &vmState, sizeof(vmState)) &&
           writeString(fd, outcome.resultJson) &&
           writeString(fd, outcome.errorCode) &&
           writeString(fd, outcome.errorMessage);
}

bool readOutcome(int fd, InternWorkerOutcome& outcome) {
    uint8_t ok = 0;
    int32_t vmState = 0;
    if (!readAll(fd, &ok, sizeof(ok)) || !readAll(fd, &vmState, sizeof(vmState))) {
        return false;
    }
    outcome.ok = ok != 0;
    outcome.vmState = static_cast<int>(vmState);
    return readString(fd, outcome.resultJson) &&
           readString(fd, outcome.errorCode) &&
           readString(fd, outcome.errorMessage);
}

void setLaunchError(std::string* launchError, const std::string& message) {
    if (launchError) {
        *launchError = message;
    }
}

} // namespace

void InternJoinSlot::store(InternWorkerOutcome value) {
    std::lock_guard<std::mutex> lock(mutex_);
    outcome_ = std::move(value);
    ready_ = true;
}

InternWorkerOutcome InternJoinSlot::load() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outcome_;
}

bool InternJoinSlot::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_;
}

void runInternWorker(InternWorkerInput input, InternJoinSlot& slot) {
    InternWorkerOutcome outcome;
    try {
        auto root = agentc::createNullValue();
        agentc::addNamedItem(root, "task_id", agentc::createStringValue(input.taskId));
        agentc::addNamedItem(root, "input", input.inputSnapshot ? input.inputSnapshot : agentc::createNullValue());
        agentc::addNamedItem(root, "context", input.contextSharedReadOnly ? input.contextSharedReadOnly : agentc::createNullValue());
        agentc::addNamedItem(root, "imports", input.importsSharedReadOnly ? input.importsSharedReadOnly : agentc::createNullValue());
        agentc::addNamedItem(root, "static_mounts", input.staticMountsReadOnly ? input.staticMountsReadOnly : agentc::createNullValue());
        agentc::addNamedItem(root, "workspace", agentc::createNullValue());

        EdictVM worker(root);
        worker.setAllowUnsafeFfiCalls(input.allowUnsafeFfiCalls);

        EdictCompiler compiler;
        const auto bytecode = compiler.compile(input.program);
        outcome.vmState = worker.execute(bytecode);
        while ((outcome.vmState & VM_YIELD) && !(outcome.vmState & VM_ERROR)) {
            if (input.cancelRequested && input.cancelRequested->load()) {
                outcome.ok = false;
                outcome.errorCode = "cancelled";
                outcome.errorMessage = "intern worker observed cancellation checkpoint";
                slot.store(std::move(outcome));
                return;
            }
            outcome.vmState = worker.resume();
        }
        if (outcome.vmState & VM_ERROR) {
            outcome.ok = false;
            outcome.errorCode = "worker_vm_error";
            outcome.errorMessage = worker.getError();
        } else {
            CPtr<agentc::ListreeValue> result = namedValue(root, "result");
            if (!result) {
                result = worker.peekData();
            }
            outcome.ok = true;
            outcome.resultJson = agentc::toJson(result ? result : agentc::createNullValue());
        }
    } catch (const std::exception& e) {
        outcome.ok = false;
        outcome.errorCode = "worker_exception";
        outcome.errorMessage = e.what();
    } catch (...) {
        outcome.ok = false;
        outcome.errorCode = "worker_unknown_exception";
        outcome.errorMessage = "unknown intern worker exception";
    }
    slot.store(std::move(outcome));
}

bool runInternWorkerForked(InternWorkerInput input,
                           InternJoinSlot& slot,
                           std::string* launchError,
                           int* childPid) {
    int pipeFds[2] = {-1, -1};
    if (::pipe(pipeFds) != 0) {
        setLaunchError(launchError, std::string("pipe failed: ") + std::strerror(errno));
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const std::string error = std::string("fork failed: ") + std::strerror(errno);
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        setLaunchError(launchError, error);
        return false;
    }

    if (pid == 0) {
        ::close(pipeFds[0]);
        InternJoinSlot childSlot;
        runInternWorker(std::move(input), childSlot);
        const InternWorkerOutcome outcome = childSlot.load();
        const bool written = writeOutcome(pipeFds[1], outcome);
        ::close(pipeFds[1]);
        _exit(written ? 0 : 1);
    }

    if (childPid) {
        *childPid = static_cast<int>(pid);
    }
    ::close(pipeFds[1]);

    InternWorkerOutcome outcome;
    const bool readOk = readOutcome(pipeFds[0], outcome);
    ::close(pipeFds[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) != pid) {
        setLaunchError(launchError, std::string("waitpid failed: ") + std::strerror(errno));
        return false;
    }
    if (!readOk) {
        setLaunchError(launchError, "worker process did not return a complete outcome");
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        setLaunchError(launchError, "worker process exited before clean completion");
        return false;
    }

    slot.store(std::move(outcome));
    return true;
}

} // namespace agentc::edict::worker
