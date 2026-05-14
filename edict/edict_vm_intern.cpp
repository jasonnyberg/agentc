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

#include "edict_vm.h"
#include "edict_compiler.h"

#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace agentc::edict {
namespace {

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value,
                                      const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

bool valueToString(CPtr<agentc::ListreeValue> value, std::string& out) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return false;
    }
    if ((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        return false;
    }
    out.assign(static_cast<const char*>(value->getData()), value->getLength());
    return true;
}

std::string stringField(CPtr<agentc::ListreeValue> value,
                        const std::string& name) {
    std::string out;
    valueToString(namedValue(value, name), out);
    return out;
}

CPtr<agentc::ListreeValue> jsonSnapshot(CPtr<agentc::ListreeValue> value) {
    if (!value) {
        return agentc::createNullValue();
    }
    auto copied = agentc::fromJson(agentc::toJson(value));
    return copied ? copied : agentc::createNullValue();
}

CPtr<agentc::ListreeValue> statusList(bool ok) {
    auto list = agentc::createListValue();
    if (ok) {
        agentc::addListItem(list, agentc::createStringValue("ok"));
    }
    return list;
}

CPtr<agentc::ListreeValue> errorObject(const std::string& code,
                                       const std::string& message) {
    auto error = agentc::createNullValue();
    agentc::addNamedItem(error, "code", agentc::createStringValue(code));
    agentc::addNamedItem(error, "message", agentc::createStringValue(message));
    return error;
}

struct InternWorkerInput {
    std::string taskId;
    std::string program;
    CPtr<agentc::ListreeValue> inputSnapshot;
    CPtr<agentc::ListreeValue> contextSharedReadOnly;
    CPtr<agentc::ListreeValue> importsSharedReadOnly;
    bool allowUnsafeFfiCalls = false;
};

struct InternWorkerOutcome {
    bool ok = false;
    int vmState = VM_NORMAL;
    std::string resultJson = "null";
    std::string errorCode;
    std::string errorMessage;
};

class InternJoinSlot {
public:
    void store(InternWorkerOutcome value) {
        std::lock_guard<std::mutex> lock(mutex_);
        outcome_ = std::move(value);
    }

    InternWorkerOutcome load() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return outcome_;
    }

private:
    mutable std::mutex mutex_;
    InternWorkerOutcome outcome_;
};

void runInternWorker(const InternWorkerInput input, InternJoinSlot& slot) {
    InternWorkerOutcome outcome;
    try {
        auto root = agentc::createNullValue();
        agentc::addNamedItem(root, "task_id", agentc::createStringValue(input.taskId));
        agentc::addNamedItem(root, "input", input.inputSnapshot ? input.inputSnapshot : agentc::createNullValue());
        agentc::addNamedItem(root, "context", input.contextSharedReadOnly ? input.contextSharedReadOnly : agentc::createNullValue());
        agentc::addNamedItem(root, "imports", input.importsSharedReadOnly ? input.importsSharedReadOnly : agentc::createNullValue());
        agentc::addNamedItem(root, "workspace", agentc::createNullValue());

        EdictVM worker(root);
        worker.setAllowUnsafeFfiCalls(input.allowUnsafeFfiCalls);

        EdictCompiler compiler;
        const auto bytecode = compiler.compile(input.program);
        outcome.vmState = worker.execute(bytecode);
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

CPtr<agentc::ListreeValue> buildInternResult(const InternWorkerInput& input,
                                             const InternWorkerOutcome& outcome) {
    auto envelope = agentc::createNullValue();
    agentc::addNamedItem(envelope, "ok", statusList(outcome.ok));
    agentc::addNamedItem(envelope, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(envelope, "state", agentc::createStringValue(outcome.ok ? "complete" : "error"));
    agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread"));

    auto result = agentc::fromJson(outcome.resultJson);
    agentc::addNamedItem(envelope, "result", result ? result : agentc::createNullValue());

    if (outcome.ok) {
        agentc::addNamedItem(envelope, "error", agentc::createNullValue());
    } else {
        agentc::addNamedItem(envelope, "error", errorObject(
            outcome.errorCode.empty() ? "worker_failed" : outcome.errorCode,
            outcome.errorMessage));
    }

    auto safety = agentc::createNullValue();
    agentc::addNamedItem(safety, "context_read_only",
                         agentc::createStringValue(input.contextSharedReadOnly && input.contextSharedReadOnly->isReadOnly() ? "true" : "false"));
    agentc::addNamedItem(safety, "imports_read_only",
                         agentc::createStringValue(input.importsSharedReadOnly && input.importsSharedReadOnly->isReadOnly() ? "true" : "false"));
    agentc::addNamedItem(safety, "input_snapshot", agentc::createStringValue("json"));
    agentc::addNamedItem(safety, "result_merge_thread", agentc::createStringValue("coordinator"));
    agentc::addNamedItem(envelope, "safety", safety);
    return envelope;
}

} // namespace

void EdictVM::op_INTERN_RUN() {
    auto task = popData();
    if (!task || task->isListMode()) {
        pushData(buildInternResult({"", "", nullptr, nullptr, nullptr, getAllowUnsafeFfiCalls()},
                                   {false, VM_ERROR, "null", "invalid_task", "intern_run expects a task object"}));
        return;
    }

    InternWorkerInput input;
    input.taskId = stringField(task, "task_id");
    if (input.taskId.empty()) {
        input.taskId = stringField(task, "id");
    }
    input.program = stringField(task, "program");
    input.allowUnsafeFfiCalls = getAllowUnsafeFfiCalls();

    if (input.taskId.empty()) {
        input.taskId = "intern-task";
    }

    if (input.program.empty()) {
        pushData(buildInternResult(input,
                                   {false, VM_ERROR, "null", "missing_program", "intern task is missing a non-empty program string"}));
        return;
    }

    input.inputSnapshot = jsonSnapshot(namedValue(task, "input"));

    input.contextSharedReadOnly = namedValue(task, "context");
    if (!input.contextSharedReadOnly) {
        input.contextSharedReadOnly = agentc::createNullValue();
    }
    if (!input.contextSharedReadOnly->isReadOnly()) {
        input.contextSharedReadOnly->setReadOnly(true);
    }

    input.importsSharedReadOnly = namedValue(task, "imports");
    if (!input.importsSharedReadOnly) {
        input.importsSharedReadOnly = agentc::createNullValue();
    }
    if (!input.importsSharedReadOnly->isReadOnly()) {
        input.importsSharedReadOnly->setReadOnly(true);
    }

    InternJoinSlot slot;
    std::thread worker(runInternWorker, input, std::ref(slot));
    worker.join();

    const auto outcome = slot.load();
    pushData(buildInternResult(input, outcome));
}

} // namespace agentc::edict
