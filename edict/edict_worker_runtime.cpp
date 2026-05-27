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
#include "static_declaration_image.h"

#include <cerrno>
#include <exception>
#include <cstdint>
#include <cstring>
#include <cstdlib>
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

std::string textValue(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->getData()) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

bool parseFd(const char* text, int& out) {
    if (!text || !*text) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 0) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
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

int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool decodeHexBytes(const std::string& hex, std::vector<uint8_t>& bytes) {
    if (hex.empty() || (hex.size() % 2) != 0) {
        return false;
    }
    std::vector<uint8_t> decoded;
    decoded.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        const int hi = hexNibble(hex[i]);
        const int lo = hexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        decoded.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    bytes = std::move(decoded);
    return true;
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

bool writeWorkerInput(int fd, const InternWorkerInput& input) {
    const uint8_t allowUnsafe = input.allowUnsafeFfiCalls ? 1 : 0;
    return writeString(fd, input.taskId) &&
           writeString(fd, input.program) &&
           writeString(fd, input.staticProgramMount) &&
           writeString(fd, input.staticProgramWord) &&
           writeString(fd, agentc::toJson(input.inputSnapshot ? input.inputSnapshot : agentc::createNullValue())) &&
           writeString(fd, agentc::toJson(input.contextSharedReadOnly ? input.contextSharedReadOnly : agentc::createNullValue())) &&
           writeString(fd, agentc::toJson(input.importsSharedReadOnly ? input.importsSharedReadOnly : agentc::createNullValue())) &&
           writeString(fd, agentc::toJson(input.staticMountsReadOnly ? input.staticMountsReadOnly : agentc::createNullValue())) &&
           writeAll(fd, &allowUnsafe, sizeof(allowUnsafe));
}

bool resolveStaticProgram(InternWorkerInput& input, std::string& error) {
    if (!input.program.empty() || input.hasStaticProgramBytecode) {
        return true;
    }
    if (input.staticProgramMount.empty() || input.staticProgramWord.empty()) {
        error = "worker task has no program or static program entry";
        return false;
    }

    auto mount = namedValue(input.staticMountsReadOnly, input.staticProgramMount);
    auto root = namedValue(mount, "root");
    auto declarations = namedValue(root, "declarations");
    if (!declarations || !declarations->isListMode()) {
        error = "static program mount does not expose declarations";
        return false;
    }

    std::string program;
    std::vector<uint8_t> bytecode;
    bool foundBytecode = false;
    declarations->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (!program.empty() || foundBytecode || !ref || !ref->getValue()) {
            return;
        }
        auto declaration = ref->getValue();
        if (textValue(namedValue(declaration, "word")) == input.staticProgramWord) {
            const std::string bytecodeHex = textValue(namedValue(declaration, "bytecode_hex"));
            if (!bytecodeHex.empty()) {
                if (!decodeHexBytes(bytecodeHex, bytecode)) {
                    error = "static program entry has invalid bytecode_hex";
                    return;
                }
                foundBytecode = true;
                return;
            }
            program = textValue(namedValue(declaration, "program_source"));
        }
    });
    if (!error.empty()) {
        return false;
    }
    if (foundBytecode) {
        input.staticProgramBytecode = std::move(bytecode);
        input.hasStaticProgramBytecode = true;
        return true;
    }
    if (program.empty()) {
        error = "static program entry not found or has no bytecode_hex/program_source";
        return false;
    }
    input.program = program;
    return true;
}

bool hydrateStaticMounts(CPtr<agentc::ListreeValue> staticMounts, std::string& error) {
    if (!staticMounts || staticMounts->isListMode()) {
        return true;
    }
    auto base = namedValue(staticMounts, "base");
    if (!base || base->isListMode()) {
        return true;
    }
    const std::string path = textValue(namedValue(base, "container_path"));
    if (path.empty()) {
        return true;
    }

    auto image = agentc::edict::static_image::readDeclarationImageContainerMmapReadOnly(path, &error);
    if (!image) {
        return false;
    }
    auto mounted = agentc::edict::static_image::mountDeclarationImageReadOnly(image);
    if (!mounted.validation.ok || !mounted.root) {
        error = mounted.validation.code + ": " + mounted.validation.message;
        return false;
    }
    auto manifest = namedValue(mounted.root, "manifest");
    const std::string module = textValue(namedValue(manifest, "module"));
    const std::string payloadHash = textValue(namedValue(manifest, "payload_hash"));
    agentc::addNamedItem(base, "source", agentc::createStringValue("g103-exec-mounted-mmap-container"));
    agentc::addNamedItem(base, "image_id", agentc::createStringValue(module + ":" + payloadHash));
    agentc::addNamedItem(base, "manifest_hash", agentc::createStringValue(payloadHash));
    agentc::addNamedItem(base, "root_descriptor", agentc::createStringValue(textValue(namedValue(manifest, "root_id"))));
    agentc::addNamedItem(base, "mounted_in_exec", agentc::createStringValue("true"));
    agentc::addNamedItem(base, "root", mounted.root);
    return true;
}

bool readWorkerInput(int fd, InternWorkerInput& input, std::string& error) {
    std::string inputJson;
    std::string contextJson;
    std::string importsJson;
    std::string staticMountsJson;
    uint8_t allowUnsafe = 0;
    if (!readString(fd, input.taskId) ||
        !readString(fd, input.program) ||
        !readString(fd, input.staticProgramMount) ||
        !readString(fd, input.staticProgramWord) ||
        !readString(fd, inputJson) ||
        !readString(fd, contextJson) ||
        !readString(fd, importsJson) ||
        !readString(fd, staticMountsJson) ||
        !readAll(fd, &allowUnsafe, sizeof(allowUnsafe))) {
        error = "worker exec input pipe did not contain a complete task";
        return false;
    }

    input.inputSnapshot = agentc::fromJson(inputJson);
    input.contextSharedReadOnly = agentc::fromJson(contextJson);
    input.importsSharedReadOnly = agentc::fromJson(importsJson);
    input.staticMountsReadOnly = agentc::fromJson(staticMountsJson);
    if (!input.inputSnapshot || !input.contextSharedReadOnly || !input.importsSharedReadOnly || !input.staticMountsReadOnly) {
        error = "worker exec input contained invalid JSON";
        return false;
    }
    if (!hydrateStaticMounts(input.staticMountsReadOnly, error)) {
        return false;
    }
    if (!resolveStaticProgram(input, error)) {
        return false;
    }
    input.contextSharedReadOnly->setReadOnly(true);
    input.importsSharedReadOnly->setReadOnly(true);
    input.staticMountsReadOnly->setReadOnly(true);
    input.allowUnsafeFfiCalls = allowUnsafe != 0;
    return true;
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
        std::string staticProgramError;
        if (!resolveStaticProgram(input, staticProgramError)) {
            outcome.ok = false;
            outcome.errorCode = "worker_static_program_error";
            outcome.errorMessage = staticProgramError;
            slot.store(std::move(outcome));
            return;
        }

        auto root = agentc::createNullValue();
        agentc::addNamedItem(root, "task_id", agentc::createStringValue(input.taskId));
        agentc::addNamedItem(root, "input", input.inputSnapshot ? input.inputSnapshot : agentc::createNullValue());
        agentc::addNamedItem(root, "context", input.contextSharedReadOnly ? input.contextSharedReadOnly : agentc::createNullValue());
        agentc::addNamedItem(root, "imports", input.importsSharedReadOnly ? input.importsSharedReadOnly : agentc::createNullValue());
        agentc::addNamedItem(root, "static_mounts", input.staticMountsReadOnly ? input.staticMountsReadOnly : agentc::createNullValue());
        agentc::addNamedItem(root, "workspace", agentc::createNullValue());

        EdictVM worker(root);
        worker.setAllowUnsafeFfiCalls(input.allowUnsafeFfiCalls);

        BytecodeBuffer bytecode;
        if (input.hasStaticProgramBytecode) {
            bytecode.getData() = input.staticProgramBytecode;
        } else {
            EdictCompiler compiler;
            bytecode = compiler.compile(input.program);
        }
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

bool launchInternWorkerForked(InternWorkerInput input,
                              InternForkedWorkerHandle& handle,
                              std::string* launchError) {
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

    ::close(pipeFds[1]);

    handle.childPid = static_cast<int>(pid);
    handle.readFd = pipeFds[0];
    return true;
}

bool collectInternWorkerForked(InternForkedWorkerHandle handle,
                               InternJoinSlot& slot,
                               std::string* launchError) {
    if (handle.childPid <= 0 || handle.readFd < 0) {
        setLaunchError(launchError, "invalid forked worker handle");
        return false;
    }

    InternWorkerOutcome outcome;
    const bool readOk = readOutcome(handle.readFd, outcome);
    ::close(handle.readFd);

    int status = 0;
    if (::waitpid(static_cast<pid_t>(handle.childPid), &status, 0) != handle.childPid) {
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

bool runInternWorkerForked(InternWorkerInput input,
                           InternJoinSlot& slot,
                           std::string* launchError,
                           int* childPid) {
    InternForkedWorkerHandle handle;
    if (!launchInternWorkerForked(std::move(input), handle, launchError)) {
        return false;
    }
    if (childPid) {
        *childPid = handle.childPid;
    }
    return collectInternWorkerForked(handle, slot, launchError);
}

bool launchInternWorkerExeced(const std::string& executablePath,
                              InternWorkerInput input,
                              InternForkedWorkerHandle& handle,
                              std::string* launchError) {
    if (executablePath.empty()) {
        setLaunchError(launchError, "worker exec path is empty");
        return false;
    }

    int inputPipe[2] = {-1, -1};
    int outputPipe[2] = {-1, -1};
    if (::pipe(inputPipe) != 0) {
        setLaunchError(launchError, std::string("input pipe failed: ") + std::strerror(errno));
        return false;
    }
    if (::pipe(outputPipe) != 0) {
        const std::string error = std::string("output pipe failed: ") + std::strerror(errno);
        ::close(inputPipe[0]);
        ::close(inputPipe[1]);
        setLaunchError(launchError, error);
        return false;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        const std::string error = std::string("fork for exec failed: ") + std::strerror(errno);
        ::close(inputPipe[0]);
        ::close(inputPipe[1]);
        ::close(outputPipe[0]);
        ::close(outputPipe[1]);
        setLaunchError(launchError, error);
        return false;
    }

    if (pid == 0) {
        ::close(inputPipe[1]);
        ::close(outputPipe[0]);
        const std::string inputFd = std::to_string(inputPipe[0]);
        const std::string outputFd = std::to_string(outputPipe[1]);
        ::execl(executablePath.c_str(), executablePath.c_str(), inputFd.c_str(), outputFd.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }

    ::close(inputPipe[0]);
    ::close(outputPipe[1]);

    const bool wrote = writeWorkerInput(inputPipe[1], input);
    ::close(inputPipe[1]);
    if (!wrote) {
        ::close(outputPipe[0]);
        int status = 0;
        (void)::waitpid(pid, &status, 0);
        setLaunchError(launchError, "worker exec input pipe write failed");
        return false;
    }

    handle.childPid = static_cast<int>(pid);
    handle.readFd = outputPipe[0];
    return true;
}

bool collectInternWorkerExeced(InternForkedWorkerHandle handle,
                               InternJoinSlot& slot,
                               std::string* launchError) {
    if (handle.childPid <= 0 || handle.readFd < 0) {
        setLaunchError(launchError, "invalid execed worker handle");
        return false;
    }

    InternWorkerOutcome outcome;
    const bool readOk = readOutcome(handle.readFd, outcome);
    ::close(handle.readFd);

    int status = 0;
    const bool waited = ::waitpid(static_cast<pid_t>(handle.childPid), &status, 0) == handle.childPid;
    if (!waited) {
        setLaunchError(launchError, std::string("worker exec waitpid failed: ") + std::strerror(errno));
        return false;
    }
    if (!readOk) {
        setLaunchError(launchError, "worker exec process did not return a complete outcome");
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        setLaunchError(launchError, "worker exec process exited before clean completion");
        return false;
    }
    slot.store(std::move(outcome));
    return true;
}

bool runInternWorkerExeced(const std::string& executablePath,
                           InternWorkerInput input,
                           InternJoinSlot& slot,
                           std::string* launchError,
                           int* childPid) {
    InternForkedWorkerHandle handle;
    if (!launchInternWorkerExeced(executablePath, std::move(input), handle, launchError)) {
        return false;
    }
    if (childPid) {
        *childPid = handle.childPid;
    }
    return collectInternWorkerExeced(handle, slot, launchError);
}

int runInternWorkerExecChildMain(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }
    int inputFd = -1;
    int outputFd = -1;
    if (!parseFd(argv[1], inputFd) || !parseFd(argv[2], outputFd)) {
        return 2;
    }

    InternWorkerInput input;
    std::string error;
    InternJoinSlot slot;
    if (readWorkerInput(inputFd, input, error)) {
        ::close(inputFd);
        runInternWorker(std::move(input), slot);
    } else {
        ::close(inputFd);
        InternWorkerOutcome outcome;
        outcome.ok = false;
        outcome.errorCode = "worker_exec_input_error";
        outcome.errorMessage = error;
        slot.store(std::move(outcome));
    }

    const InternWorkerOutcome outcome = slot.load();
    const bool written = writeOutcome(outputFd, outcome);
    ::close(outputFd);
    return written ? 0 : 1;
}

} // namespace agentc::edict::worker
