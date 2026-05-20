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

#include "edict_intern_service.h"
#include "edict_worker_runtime.h"
#include "../core/root1_resource_broker.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

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

bool sizeField(CPtr<agentc::ListreeValue> value,
               const std::string& name,
               size_t& out) {
    const std::string text = stringField(value, name);
    if (text.empty()) {
        return false;
    }
    try {
        size_t parsedChars = 0;
        const unsigned long long parsed = std::stoull(text, &parsedChars, 10);
        if (parsedChars != text.size()) {
            return false;
        }
        out = static_cast<size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool hasExplicitFalseOk(CPtr<agentc::ListreeValue> value) {
    auto ok = namedValue(value, "ok");
    if (!ok || !ok->isListMode()) {
        return false;
    }
    bool any = false;
    ok->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            any = true;
        }
    });
    return !any;
}

bool valueTruthy(CPtr<agentc::ListreeValue> value) {
    if (!value) {
        return false;
    }
    if (value->isListMode()) {
        bool any = false;
        value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
            if (ref && valueTruthy(ref->getValue())) {
                any = true;
            }
        });
        return any;
    }
    if (value->getData() && value->getLength() > 0) {
        return true;
    }
    bool anyNamed = false;
    value->forEachTree([&](const std::string&, CPtr<agentc::ListreeItem>& item) {
        if (item && valueTruthy(item->getValue(false, false))) {
            anyNamed = true;
        }
    });
    return anyNamed;
}

size_t evidenceCount(CPtr<agentc::ListreeValue> evidence) {
    if (!evidence) {
        return 0;
    }
    if (!evidence->isListMode()) {
        return valueTruthy(evidence) ? 1u : 0u;
    }
    size_t count = 0;
    evidence->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && valueTruthy(ref->getValue())) {
            ++count;
        }
    });
    return count;
}

int confidenceRank(const std::string& confidence) {
    if (confidence == "low") {
        return 1;
    }
    if (confidence == "medium") {
        return 2;
    }
    if (confidence == "high") {
        return 3;
    }
    return 0;
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

using worker::InternJoinSlot;
using worker::InternWorkerInput;
using worker::InternWorkerOutcome;
using worker::runInternWorker;

CPtr<agentc::ListreeValue> buildPrepareErrorStatus(const InternWorkerInput& input,
                                                     const std::string& code,
                                                     const std::string& message) {
    auto status = agentc::createNullValue();
    agentc::addNamedItem(status, "ok", statusList(false));
    agentc::addNamedItem(status, "kind", agentc::createStringValue("worker_prepare_status"));
    agentc::addNamedItem(status, "state", agentc::createStringValue("error"));
    agentc::addNamedItem(status, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(status, "worker", agentc::createStringValue("edict-thread"));
    agentc::addNamedItem(status, "job_id", agentc::createNullValue());
    agentc::addNamedItem(status, "waitable", agentc::createNullValue());
    agentc::addNamedItem(status, "result", agentc::createNullValue());
    agentc::addNamedItem(status, "safety", agentc::createNullValue());
    agentc::addNamedItem(status, "publication", agentc::createNullValue());
    agentc::addNamedItem(status, "events", agentc::createListValue());
    agentc::addNamedItem(status, "error", errorObject(code, message));
    agentc::addNamedItem(status, "error_code", agentc::createStringValue(code));
    agentc::addNamedItem(status, "error_message", agentc::createStringValue(message));
    return status;
}

bool parseInternTask(CPtr<agentc::ListreeValue> task,
                     bool allowUnsafeFfiCalls,
                     const std::string& opName,
                     InternWorkerInput& input,
                     CPtr<agentc::ListreeValue>& errorEnvelope) {
    if (!task || task->isListMode()) {
        input.allowUnsafeFfiCalls = allowUnsafeFfiCalls;
        errorEnvelope = buildPrepareErrorStatus(input,
                                                "invalid_task",
                                                opName + " expects a task object");
        return false;
    }

    input.taskId = stringField(task, "task_id");
    if (input.taskId.empty()) {
        input.taskId = stringField(task, "id");
    }
    input.program = stringField(task, "program");
    input.allowUnsafeFfiCalls = allowUnsafeFfiCalls;
    size_t maxActiveJobs = 0;
    if (sizeField(task, "max_active_jobs", maxActiveJobs)) {
        input.hasMaxActiveJobs = true;
        input.maxActiveJobs = maxActiveJobs;
    }

    if (input.taskId.empty()) {
        input.taskId = "intern-task";
    }

    if (input.program.empty()) {
        errorEnvelope = buildPrepareErrorStatus(input,
                                                "missing_program",
                                                "intern task is missing a non-empty program string");
        return false;
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

    return true;
}

CPtr<agentc::ListreeValue> buildPreparedTaskSpec(const InternWorkerInput& input) {
    auto spec = agentc::createNullValue();
    agentc::addNamedItem(spec, "ok", statusList(true));
    agentc::addNamedItem(spec, "state", agentc::createStringValue("prepared"));
    agentc::addNamedItem(spec, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(spec, "program", agentc::createStringValue(input.program));
    agentc::addNamedItem(spec, "input", input.inputSnapshot ? input.inputSnapshot : agentc::createNullValue());
    agentc::addNamedItem(spec, "context", input.contextSharedReadOnly ? input.contextSharedReadOnly : agentc::createNullValue());
    agentc::addNamedItem(spec, "imports", input.importsSharedReadOnly ? input.importsSharedReadOnly : agentc::createNullValue());
    if (input.hasMaxActiveJobs) {
        agentc::addNamedItem(spec, "max_active_jobs", agentc::createStringValue(std::to_string(input.maxActiveJobs)));
    } else {
        agentc::addNamedItem(spec, "max_active_jobs", agentc::createNullValue());
    }
    agentc::addNamedItem(spec, "error", agentc::createNullValue());
    agentc::addNamedItem(spec, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(spec, "publication", agentc::createNullValue());
    return spec;
}

CPtr<agentc::ListreeValue> buildCapacityStatusEnvelope(const InternWorkerInput& input,
                                                       size_t activeJobs) {
    const bool allowed = !input.hasMaxActiveJobs || activeJobs < input.maxActiveJobs;
    auto envelope = agentc::createNullValue();
    agentc::addNamedItem(envelope, "ok", statusList(true));
    agentc::addNamedItem(envelope, "state", agentc::createStringValue("capacity"));
    agentc::addNamedItem(envelope, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(envelope, "active_jobs", agentc::createStringValue(std::to_string(activeJobs)));
    if (input.hasMaxActiveJobs) {
        agentc::addNamedItem(envelope, "max_active_jobs", agentc::createStringValue(std::to_string(input.maxActiveJobs)));
    } else {
        agentc::addNamedItem(envelope, "max_active_jobs", agentc::createNullValue());
    }
    agentc::addNamedItem(envelope, "allowed", statusList(allowed));
    agentc::addNamedItem(envelope, "message", agentc::createStringValue(
        allowed
            ? "intern job capacity available"
            : "intern job limit reached: active=" + std::to_string(activeJobs) +
                  " max=" + std::to_string(input.maxActiveJobs)));
    agentc::addNamedItem(envelope, "error", agentc::createNullValue());
    agentc::addNamedItem(envelope, "events", agentc::createListValue());
    agentc::addNamedItem(envelope, "publication", agentc::createNullValue());
    return envelope;
}

std::string descriptorEventName(agentc::root1::MailboxEventKind kind) {
    using agentc::root1::MailboxEventKind;
    switch (kind) {
        case MailboxEventKind::None: return "none";
        case MailboxEventKind::Message: return "message";
        case MailboxEventKind::OwnershipGranted: return "ownership_granted";
        case MailboxEventKind::Progress: return "progress";
        case MailboxEventKind::Complete: return "complete";
        case MailboxEventKind::Error: return "error";
        case MailboxEventKind::Cancelled: return "cancelled";
        case MailboxEventKind::Backpressure: return "backpressure";
        case MailboxEventKind::OwnerDied: return "owner_died";
    }
    return "unknown";
}

CPtr<agentc::ListreeValue> descriptorToValue(const agentc::root1::MailboxDescriptor& descriptor) {
    auto value = agentc::createNullValue();
    agentc::addNamedItem(value, "kind", agentc::createStringValue(descriptorEventName(descriptor.eventKind)));
    agentc::addNamedItem(value, "sequence", agentc::createStringValue(std::to_string(descriptor.sequence)));
    agentc::addNamedItem(value, "correlation_id", agentc::createStringValue(std::to_string(descriptor.correlationId)));
    agentc::addNamedItem(value, "grant_token", agentc::createStringValue(std::to_string(descriptor.grantToken)));
    const auto payload = agentc::root1::inlinePayload(descriptor);
    if (!payload.empty()) {
        agentc::addNamedItem(value, "payload", agentc::createStringValue(std::string(payload)));
    } else {
        agentc::addNamedItem(value, "payload", agentc::createNullValue());
    }
    return value;
}

CPtr<agentc::ListreeValue> descriptorsToList(const std::vector<agentc::root1::MailboxDescriptor>& descriptors) {
    auto list = agentc::createListValue();
    for (const auto& descriptor : descriptors) {
        agentc::addListItem(list, descriptorToValue(descriptor));
    }
    return list;
}

bool eventListEmpty(CPtr<agentc::ListreeValue> events) {
    if (!events || !events->isListMode()) {
        return true;
    }
    bool empty = true;
    events->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            empty = false;
        }
    });
    return empty;
}

CPtr<agentc::ListreeValue> ensureEventList(CPtr<agentc::ListreeValue> events) {
    if (events && events->isListMode()) {
        return events;
    }
    return agentc::createListValue();
}

agentc::root1::MailboxDescriptor makeDescriptor(agentc::root1::MailboxEventKind kind,
                                                uint64_t correlationId,
                                                const std::string& payload) {
    agentc::root1::MailboxDescriptor descriptor;
    descriptor.eventKind = kind;
    descriptor.correlationId = correlationId;
    descriptor.sequence = correlationId;
    if (!payload.empty()) {
        agentc::root1::setInlinePayload(descriptor, payload);
    }
    return descriptor;
}

CPtr<agentc::ListreeValue> singletonDescriptorList(agentc::root1::MailboxEventKind kind,
                                                   uint64_t correlationId,
                                                   const std::string& payload) {
    std::vector<agentc::root1::MailboxDescriptor> descriptors;
    descriptors.push_back(makeDescriptor(kind, correlationId, payload));
    return descriptorsToList(descriptors);
}

CPtr<agentc::ListreeValue> waitableValue(const std::string& jobId,
                                         agentc::root1::ParticipantId participant) {
    auto waitable = agentc::createNullValue();
    agentc::addNamedItem(waitable, "kind", agentc::createStringValue("root1-broker"));
    agentc::addNamedItem(waitable, "job_id", agentc::createStringValue(jobId));
    agentc::addNamedItem(waitable, "participant_id", agentc::createStringValue(std::to_string(participant)));
    return waitable;
}

CPtr<agentc::ListreeValue> buildSafetyStatus(const InternWorkerInput& input) {
    auto safety = agentc::createNullValue();
    agentc::addNamedItem(safety, "context_read_only",
                         agentc::createStringValue(input.contextSharedReadOnly && input.contextSharedReadOnly->isReadOnly() ? "true" : "false"));
    agentc::addNamedItem(safety, "imports_read_only",
                         agentc::createStringValue(input.importsSharedReadOnly && input.importsSharedReadOnly->isReadOnly() ? "true" : "false"));
    agentc::addNamedItem(safety, "input_snapshot", agentc::createStringValue("json"));
    agentc::addNamedItem(safety, "result_merge_thread", agentc::createStringValue("coordinator"));
    return safety;
}

CPtr<agentc::ListreeValue> buildUnknownJobStatus(const std::string& jobId,
                                                 CPtr<agentc::ListreeValue> events) {
    InternWorkerInput input;
    input.taskId = jobId;

    auto status = agentc::createNullValue();
    agentc::addNamedItem(status, "ok", statusList(true));
    agentc::addNamedItem(status, "kind", agentc::createStringValue("worker_collect_status"));
    agentc::addNamedItem(status, "found", statusList(false));
    agentc::addNamedItem(status, "terminal", statusList(true));
    agentc::addNamedItem(status, "cancel_requested", statusList(false));
    agentc::addNamedItem(status, "cancelled", statusList(false));
    agentc::addNamedItem(status, "worker_ok", statusList(false));
    agentc::addNamedItem(status, "job_id", agentc::createStringValue(jobId));
    agentc::addNamedItem(status, "task_id", agentc::createStringValue(jobId));
    agentc::addNamedItem(status, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(status, "waitable", agentc::createNullValue());
    agentc::addNamedItem(status, "result", agentc::createNullValue());
    agentc::addNamedItem(status, "safety", buildSafetyStatus(input));
    agentc::addNamedItem(status, "error_code", agentc::createStringValue("unknown_job"));
    agentc::addNamedItem(status, "error_message", agentc::createStringValue("unknown intern job id: " + jobId));
    agentc::addNamedItem(status, "events", ensureEventList(events));
    agentc::addNamedItem(status, "default_events", agentc::createListValue());
    agentc::addNamedItem(status, "publication", agentc::createNullValue());
    return status;
}

CPtr<agentc::ListreeValue> buildBlockingRunStatus(const InternWorkerInput& input,
                                                  const InternWorkerOutcome& outcome) {
    auto status = agentc::createNullValue();
    agentc::addNamedItem(status, "ok", statusList(true));
    agentc::addNamedItem(status, "kind", agentc::createStringValue("worker_run_status"));
    agentc::addNamedItem(status, "terminal", statusList(true));
    agentc::addNamedItem(status, "worker_ok", statusList(outcome.ok));
    agentc::addNamedItem(status, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(status, "worker", agentc::createStringValue("edict-thread"));
    auto result = agentc::fromJson(outcome.resultJson);
    agentc::addNamedItem(status, "result", result ? result : agentc::createNullValue());
    agentc::addNamedItem(status, "safety", buildSafetyStatus(input));
    agentc::addNamedItem(status, "error_code", agentc::createStringValue(
        outcome.ok ? "" : (outcome.errorCode.empty() ? "worker_failed" : outcome.errorCode)));
    agentc::addNamedItem(status, "error_message", agentc::createStringValue(outcome.ok ? "" : outcome.errorMessage));
    agentc::addNamedItem(status, "publication", agentc::createNullValue());
    return status;
}

CPtr<agentc::ListreeValue> buildDropEnvelope(const std::string& jobId,
                                             const std::string& state,
                                             bool ok,
                                             const std::string& code = "",
                                             const std::string& message = "") {
    auto envelope = agentc::createNullValue();
    agentc::addNamedItem(envelope, "ok", statusList(ok));
    agentc::addNamedItem(envelope, "state", agentc::createStringValue(state));
    agentc::addNamedItem(envelope, "job_id", agentc::createStringValue(jobId));
    agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(envelope, "publication", agentc::createNullValue());
    if (ok) {
        agentc::addNamedItem(envelope, "error", agentc::createNullValue());
    } else {
        agentc::addNamedItem(envelope, "error", errorObject(code, message));
    }
    return envelope;
}

std::string jobIdFromValue(CPtr<agentc::ListreeValue> value) {
    std::string jobId;
    if (!value) {
        return jobId;
    }
    if (!value->isListMode() && valueToString(value, jobId)) {
        return jobId;
    }
    jobId = stringField(value, "job_id");
    if (jobId.empty()) {
        jobId = stringField(value, "id");
    }
    return jobId;
}

struct AsyncInternJob {
    uint64_t numericId = 0;
    std::string jobId;
    agentc::root1::ParticipantId participant = 0;
    InternWorkerInput input;
    std::shared_ptr<InternJoinSlot> slot;
    std::shared_ptr<std::atomic<bool>> cancelRequested;
    std::atomic<bool> abandoned{false};
    bool terminalObserved = false;
    uint64_t terminalSequence = 0;
};

CPtr<agentc::ListreeValue> buildWorkerStartStatus(const AsyncInternJob& job) {
    auto status = agentc::createNullValue();
    agentc::addNamedItem(status, "ok", statusList(true));
    agentc::addNamedItem(status, "kind", agentc::createStringValue("worker_start_status"));
    agentc::addNamedItem(status, "started", statusList(true));
    agentc::addNamedItem(status, "task_id", agentc::createStringValue(job.input.taskId));
    agentc::addNamedItem(status, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(status, "job_id", agentc::createStringValue(job.jobId));
    agentc::addNamedItem(status, "waitable", waitableValue(job.jobId, job.participant));
    agentc::addNamedItem(status, "publication", agentc::createNullValue());
    agentc::addNamedItem(status, "events", agentc::createListValue());
    agentc::addNamedItem(status, "error_code", agentc::createStringValue(""));
    agentc::addNamedItem(status, "error_message", agentc::createStringValue(""));
    return status;
}

CPtr<agentc::ListreeValue> buildWorkerStartContractErrorStatus(CPtr<agentc::ListreeValue> task,
                                                                 const std::string& code,
                                                                 const std::string& message) {
    InternWorkerInput input;
    input.taskId = stringField(task, "task_id");
    if (input.taskId.empty()) {
        input.taskId = stringField(task, "id");
    }
    if (input.taskId.empty()) {
        input.taskId = "intern-task";
    }

    auto status = agentc::createNullValue();
    agentc::addNamedItem(status, "ok", statusList(false));
    agentc::addNamedItem(status, "kind", agentc::createStringValue("worker_start_status"));
    agentc::addNamedItem(status, "state", agentc::createStringValue("error"));
    agentc::addNamedItem(status, "started", statusList(false));
    agentc::addNamedItem(status, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(status, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(status, "job_id", agentc::createNullValue());
    agentc::addNamedItem(status, "waitable", agentc::createNullValue());
    agentc::addNamedItem(status, "publication", agentc::createNullValue());
    agentc::addNamedItem(status, "error_code", agentc::createStringValue(code));
    agentc::addNamedItem(status, "error_message", agentc::createStringValue(message));
    agentc::addNamedItem(status, "events", agentc::createListValue());
    return status;
}

bool validateStartContract(CPtr<agentc::ListreeValue> task,
                           std::string& code,
                           std::string& message) {
    if (!task || task->isListMode()) {
        code = "invalid_task";
        message = "intern_start requires a task object";
        return false;
    }
    if (stringField(task, "program").empty()) {
        code = "missing_program";
        message = "intern task contract requires a non-empty program";
        return false;
    }
    auto expect = namedValue(task, "expect");
    if (stringField(expect, "success_field").empty()) {
        code = "missing_success_field";
        message = "intern task contract requires expect.success_field";
        return false;
    }
    auto limits = namedValue(task, "limits");
    if (stringField(limits, "max_result_bytes").empty()) {
        code = "missing_result_limit";
        message = "intern task contract requires limits.max_result_bytes";
        return false;
    }
    return true;
}

CPtr<agentc::ListreeValue> buildWorkerStartBackpressureStatus(const InternWorkerInput& input,
                                                              size_t activeJobs,
                                                              size_t maxActiveJobs) {
    const std::string message = "intern job limit reached: active=" + std::to_string(activeJobs) +
                                " max=" + std::to_string(maxActiveJobs);
    auto status = agentc::createNullValue();
    agentc::addNamedItem(status, "ok", statusList(true));
    agentc::addNamedItem(status, "kind", agentc::createStringValue("worker_start_status"));
    agentc::addNamedItem(status, "state", agentc::createStringValue("backpressure"));
    agentc::addNamedItem(status, "started", statusList(false));
    agentc::addNamedItem(status, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(status, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(status, "job_id", agentc::createNullValue());
    agentc::addNamedItem(status, "waitable", agentc::createNullValue());
    agentc::addNamedItem(status, "publication", agentc::createNullValue());
    agentc::addNamedItem(status, "active_jobs", agentc::createStringValue(std::to_string(activeJobs)));
    agentc::addNamedItem(status, "max_active_jobs", agentc::createStringValue(std::to_string(maxActiveJobs)));
    agentc::addNamedItem(status, "error_code", agentc::createStringValue("backpressure"));
    agentc::addNamedItem(status, "error_message", agentc::createStringValue(message));
    agentc::addNamedItem(status, "events", singletonDescriptorList(
        agentc::root1::MailboxEventKind::Backpressure,
        0,
        "intern job limit reached"));
    return status;
}

CPtr<agentc::ListreeValue> buildWorkerCollectStatus(const AsyncInternJob& job,
                                                    bool ready,
                                                    bool cancelRequested,
                                                    const InternWorkerOutcome* outcome,
                                                    CPtr<agentc::ListreeValue> events) {
    auto status = agentc::createNullValue();
    agentc::addNamedItem(status, "ok", statusList(true));
    agentc::addNamedItem(status, "kind", agentc::createStringValue("worker_collect_status"));
    agentc::addNamedItem(status, "found", statusList(true));
    agentc::addNamedItem(status, "terminal", statusList(ready));
    agentc::addNamedItem(status, "cancel_requested", statusList(cancelRequested && !ready));
    agentc::addNamedItem(status, "cancelled", statusList(cancelRequested && ready));
    agentc::addNamedItem(status, "worker_ok", statusList(ready && !cancelRequested && outcome && outcome->ok));
    agentc::addNamedItem(status, "job_id", agentc::createStringValue(job.jobId));
    agentc::addNamedItem(status, "task_id", agentc::createStringValue(job.input.taskId));
    agentc::addNamedItem(status, "worker", agentc::createStringValue("edict-thread-async"));
    CPtr<agentc::ListreeValue> visibleEvents = ensureEventList(events);
    if (ready && cancelRequested) {
        visibleEvents = singletonDescriptorList(agentc::root1::MailboxEventKind::Cancelled,
                                                job.numericId,
                                                "intern job cancellation requested");
    }

    agentc::addNamedItem(status, "waitable", waitableValue(job.jobId, job.participant));
    agentc::addNamedItem(status, "publication", agentc::createNullValue());
    agentc::addNamedItem(status, "events", visibleEvents);
    agentc::addNamedItem(status, "safety", buildSafetyStatus(job.input));

    if (!ready) {
        agentc::addNamedItem(status, "result", agentc::createNullValue());
        agentc::addNamedItem(status, "error_code", agentc::createStringValue(""));
        agentc::addNamedItem(status, "error_message", agentc::createStringValue(""));
        agentc::addNamedItem(status, "default_events", agentc::createListValue());
        return status;
    }

    if (cancelRequested) {
        agentc::addNamedItem(status, "result", agentc::createNullValue());
        agentc::addNamedItem(status, "error_code", agentc::createStringValue("cancelled"));
        agentc::addNamedItem(status, "error_message", agentc::createStringValue("intern job was cancelled before result collection"));
        agentc::addNamedItem(status, "default_events", singletonDescriptorList(
            agentc::root1::MailboxEventKind::Cancelled,
            job.numericId,
            "intern job cancellation requested"));
        return status;
    }

    const bool workerOk = outcome && outcome->ok;
    if (eventListEmpty(visibleEvents)) {
        visibleEvents = singletonDescriptorList(
            workerOk ? agentc::root1::MailboxEventKind::Complete : agentc::root1::MailboxEventKind::Error,
            job.numericId,
            workerOk ? "complete" : "error");
        agentc::addNamedItem(status, "events", visibleEvents);
    }
    auto result = outcome ? agentc::fromJson(outcome->resultJson) : nullptr;
    agentc::addNamedItem(status, "result", result ? result : agentc::createNullValue());
    agentc::addNamedItem(status, "error_code", agentc::createStringValue(
        workerOk ? "" : (outcome && !outcome->errorCode.empty() ? outcome->errorCode : "worker_failed")));
    agentc::addNamedItem(status, "error_message", agentc::createStringValue(workerOk || !outcome ? "" : outcome->errorMessage));
    agentc::addNamedItem(status, "default_events", singletonDescriptorList(
        workerOk ? agentc::root1::MailboxEventKind::Complete : agentc::root1::MailboxEventKind::Error,
        job.numericId,
        workerOk ? "complete" : "error"));
    return status;
}

class InternJobManager {
public:
    size_t activeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeCountLocked();
    }

    CPtr<agentc::ListreeValue> startStatus(InternWorkerInput input) {
        const size_t maxActiveJobs = input.hasMaxActiveJobs ? input.maxActiveJobs : defaultMaxActiveJobs_;
        return startStatusWithLimit(std::move(input), maxActiveJobs);
    }

    CPtr<agentc::ListreeValue> startPreparedStatus(InternWorkerInput input) {
        return startStatusWithLimit(std::move(input), defaultMaxActiveJobs_);
    }

    std::shared_ptr<AsyncInternJob> launchJob(InternWorkerInput input) {
        auto job = std::make_shared<AsyncInternJob>();
        job->numericId = nextJobId_++;
        job->jobId = "intern-job-" + std::to_string(job->numericId);
        job->participant = broker_.registerParticipant();
        job->input = std::move(input);
        job->slot = std::make_shared<InternJoinSlot>();
        job->cancelRequested = std::make_shared<std::atomic<bool>>(false);
        job->input.cancelRequested = job->cancelRequested;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_[job->jobId] = job;
        }

        std::thread([this, job]() {
            runInternWorker(job->input, *job->slot);
            if (job->abandoned.load()) {
                return;
            }
            const auto outcome = job->slot->load();
            auto descriptor = makeDescriptor(
                outcome.ok
                    ? agentc::root1::MailboxEventKind::Complete
                    : agentc::root1::MailboxEventKind::Error,
                job->numericId,
                outcome.ok ? "complete" : "error");
            broker_.sendMailboxDescriptor(job->participant, descriptor);
        }).detach();

        return job;
    }

    CPtr<agentc::ListreeValue> startStatusWithLimit(InternWorkerInput input,
                                                    size_t maxActiveJobs) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const size_t activeJobs = activeCountLocked();
            if (activeJobs >= maxActiveJobs) {
                return buildWorkerStartBackpressureStatus(input, activeJobs, maxActiveJobs);
            }
        }

        auto job = launchJob(std::move(input));
        return buildWorkerStartStatus(*job);
    }

    CPtr<agentc::ListreeValue> requestCancelEvents(const std::string& jobId) {
        std::shared_ptr<AsyncInternJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(jobId);
            if (it == jobs_.end()) {
                return agentc::createListValue();
            }
            job = it->second;
        }

        if (job->slot->ready()) {
            const auto outcome = job->slot->load();
            return singletonDescriptorList(outcome.ok
                                               ? agentc::root1::MailboxEventKind::Complete
                                               : agentc::root1::MailboxEventKind::Error,
                                           job->numericId,
                                           outcome.ok ? "complete" : "error");
        }

        const bool firstRequest = job->cancelRequested && !job->cancelRequested->exchange(true);
        if (firstRequest) {
            broker_.sendCancellation(job->participant,
                                     job->numericId,
                                     "intern job cancellation requested");
        }
        return singletonDescriptorList(agentc::root1::MailboxEventKind::Cancelled,
                                       job->numericId,
                                       "intern job cancellation requested");
    }

    CPtr<agentc::ListreeValue> drainEvents(const std::string& jobId) {
        std::shared_ptr<AsyncInternJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(jobId);
            if (it == jobs_.end()) {
                return agentc::createListValue();
            }
            job = it->second;
        }

        broker_.pollReadyParticipants(0);
        return descriptorsToList(broker_.drainMailboxDescriptors(job->participant));
    }

    CPtr<agentc::ListreeValue> collectStatus(const std::string& jobId,
                                             CPtr<agentc::ListreeValue> providedEvents) {
        std::shared_ptr<AsyncInternJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(jobId);
            if (it == jobs_.end()) {
                return buildUnknownJobStatus(jobId, providedEvents);
            }
            job = it->second;
        }

        const bool ready = job->slot->ready();
        const bool cancelRequested = job->cancelRequested && job->cancelRequested->load();
        if (!ready) {
            return buildWorkerCollectStatus(*job, false, cancelRequested, nullptr, providedEvents);
        }

        const auto outcome = job->slot->load();
        auto status = buildWorkerCollectStatus(*job, true, cancelRequested, &outcome, providedEvents);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(job->jobId);
            if (it != jobs_.end()) {
                it->second->terminalObserved = true;
                if (it->second->terminalSequence == 0) {
                    it->second->terminalSequence = nextTerminalSequence_++;
                }
                sweepRetainedTerminalJobsLocked();
            }
        }
        return status;
    }

    CPtr<agentc::ListreeValue> drop(const std::string& jobId) {
        std::shared_ptr<AsyncInternJob> job;
        bool ready = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(jobId);
            if (it == jobs_.end()) {
                return buildDropEnvelope(jobId,
                                         "error",
                                         false,
                                         "unknown_job",
                                         "unknown intern job id: " + jobId);
            }
            job = it->second;
            ready = job->slot->ready();
            if (!ready) {
                job->abandoned.store(true);
            }
            jobs_.erase(it);
        }

        if (ready) {
            return buildDropEnvelope(jobId, "dropped", true);
        }

        return buildDropEnvelope(jobId, "abandoned", true);
    }

private:
    size_t activeCountLocked() const {
        size_t active = 0;
        for (const auto& entry : jobs_) {
            const auto& job = entry.second;
            if (job && !job->slot->ready() && !job->abandoned.load()) {
                ++active;
            }
        }
        return active;
    }

    void sweepRetainedTerminalJobsLocked() {
        std::vector<std::shared_ptr<AsyncInternJob>> terminalJobs;
        for (const auto& entry : jobs_) {
            const auto& job = entry.second;
            if (job && job->terminalObserved && job->slot->ready()) {
                terminalJobs.push_back(job);
            }
        }
        if (terminalJobs.size() <= maxRetainedTerminalJobs_) {
            return;
        }
        std::sort(terminalJobs.begin(), terminalJobs.end(), [](const auto& lhs, const auto& rhs) {
            return lhs->terminalSequence < rhs->terminalSequence;
        });
        const size_t toErase = terminalJobs.size() - maxRetainedTerminalJobs_;
        for (size_t i = 0; i < toErase; ++i) {
            jobs_.erase(terminalJobs[i]->jobId);
        }
    }

    mutable std::mutex mutex_;
    agentc::root1::Root1ResourceBroker broker_;
    uint64_t nextJobId_ = 1;
    uint64_t nextTerminalSequence_ = 1;
    size_t defaultMaxActiveJobs_ = 64;
    size_t maxRetainedTerminalJobs_ = 64;
    std::unordered_map<std::string, std::shared_ptr<AsyncInternJob>> jobs_;
};

InternJobManager& internJobManager() {
    // Intentionally leak the process-lifetime manager: async jobs use detached
    // worker threads that may still be unwinding during process shutdown.  The
    // OS will reclaim eventfds/threads on exit; running the broker destructor
    // while a detached worker can still publish a completion descriptor would
    // be unsafe.
    static auto* manager = new InternJobManager();
    return *manager;
}

} // namespace

namespace intern {

CPtr<agentc::ListreeValue> activeCount() {
    return agentc::createStringValue(std::to_string(internJobManager().activeCount()));
}

CPtr<agentc::ListreeValue> prepareTask(CPtr<agentc::ListreeValue> task,
                                       bool allowUnsafeFfiCalls) {
    InternWorkerInput input;
    CPtr<agentc::ListreeValue> errorEnvelope;
    if (!parseInternTask(task, allowUnsafeFfiCalls, "intern_prepare_task", input, errorEnvelope)) {
        return errorEnvelope;
    }
    return buildPreparedTaskSpec(input);
}

CPtr<agentc::ListreeValue> capacityStatus(CPtr<agentc::ListreeValue> taskOrSpec,
                                          bool allowUnsafeFfiCalls) {
    if (hasExplicitFalseOk(taskOrSpec)) {
        return taskOrSpec;
    }

    InternWorkerInput input;
    CPtr<agentc::ListreeValue> errorEnvelope;
    if (!parseInternTask(taskOrSpec, allowUnsafeFfiCalls, "intern_capacity_status", input, errorEnvelope)) {
        return errorEnvelope;
    }

    return buildCapacityStatusEnvelope(input, internJobManager().activeCount());
}

CPtr<agentc::ListreeValue> runStatusPrepared(CPtr<agentc::ListreeValue> preparedTask,
                                             bool allowUnsafeFfiCalls) {
    if (hasExplicitFalseOk(preparedTask)) {
        return preparedTask;
    }

    InternWorkerInput input;
    CPtr<agentc::ListreeValue> errorEnvelope;
    if (!parseInternTask(preparedTask, allowUnsafeFfiCalls, "intern_run_status_prepared", input, errorEnvelope)) {
        return errorEnvelope;
    }

    InternJoinSlot slot;
    std::thread worker(runInternWorker, input, std::ref(slot));
    worker.join();

    const auto outcome = slot.load();
    return buildBlockingRunStatus(input, outcome);
}

CPtr<agentc::ListreeValue> runStatus(CPtr<agentc::ListreeValue> task,
                                     bool allowUnsafeFfiCalls) {
    InternWorkerInput input;
    CPtr<agentc::ListreeValue> errorEnvelope;
    if (!parseInternTask(task, allowUnsafeFfiCalls, "intern_run_status", input, errorEnvelope)) {
        return errorEnvelope;
    }

    InternJoinSlot slot;
    std::thread worker(runInternWorker, input, std::ref(slot));
    worker.join();

    const auto outcome = slot.load();
    return buildBlockingRunStatus(input, outcome);
}

CPtr<agentc::ListreeValue> startStatusPrepared(CPtr<agentc::ListreeValue> preparedTask,
                                               bool allowUnsafeFfiCalls) {
    if (hasExplicitFalseOk(preparedTask)) {
        return preparedTask;
    }

    InternWorkerInput input;
    CPtr<agentc::ListreeValue> errorEnvelope;
    if (!parseInternTask(preparedTask, allowUnsafeFfiCalls, "intern_start_status_prepared", input, errorEnvelope)) {
        return errorEnvelope;
    }
    return internJobManager().startPreparedStatus(input);
}

CPtr<agentc::ListreeValue> startStatus(CPtr<agentc::ListreeValue> task,
                                       bool allowUnsafeFfiCalls) {
    std::string contractCode;
    std::string contractMessage;
    if (!validateStartContract(task, contractCode, contractMessage)) {
        return buildWorkerStartContractErrorStatus(task, contractCode, contractMessage);
    }

    InternWorkerInput input;
    CPtr<agentc::ListreeValue> errorEnvelope;
    if (!parseInternTask(task, allowUnsafeFfiCalls, "intern_start_status", input, errorEnvelope)) {
        return errorEnvelope;
    }
    return internJobManager().startStatus(input);
}

CPtr<agentc::ListreeValue> drainEvents(CPtr<agentc::ListreeValue> jobOrRequest) {
    const std::string jobId = jobIdFromValue(jobOrRequest);
    if (jobId.empty()) {
        return agentc::createListValue();
    }
    return internJobManager().drainEvents(jobId);
}

CPtr<agentc::ListreeValue> requestCancel(CPtr<agentc::ListreeValue> jobOrRequest) {
    const std::string jobId = jobIdFromValue(jobOrRequest);
    if (jobId.empty()) {
        return agentc::createListValue();
    }
    return internJobManager().requestCancelEvents(jobId);
}

CPtr<agentc::ListreeValue> collectStatus(CPtr<agentc::ListreeValue> jobOrRequest,
                                         CPtr<agentc::ListreeValue> events) {
    bool cancel = false;
    if (jobOrRequest && !jobOrRequest->isListMode()) {
        const std::string action = stringField(jobOrRequest, "action");
        cancel = (action == "cancel" || stringField(jobOrRequest, "op") == "cancel");
    }

    const std::string jobId = jobIdFromValue(jobOrRequest);
    if (jobId.empty()) {
        return buildUnknownJobStatus("", events);
    }
    return cancel ? internJobManager().collectStatus(jobId, internJobManager().requestCancelEvents(jobId))
                  : internJobManager().collectStatus(jobId, events);
}

CPtr<agentc::ListreeValue> validateResultContract(CPtr<agentc::ListreeValue> check) {
    auto envelope = namedValue(check, "envelope");
    auto expect = namedValue(check, "expect");
    auto result = namedValue(envelope, "result");
    const std::string taskId = stringField(envelope, "task_id");
    const std::string envelopeState = stringField(envelope, "state");

    auto status = agentc::createNullValue();
    agentc::addNamedItem(status, "kind", agentc::createStringValue("intern_result_contract"));
    agentc::addNamedItem(status, "task_id", taskId.empty() ? agentc::createNullValue() : agentc::createStringValue(taskId));
    agentc::addNamedItem(status, "envelope_state", envelopeState.empty() ? agentc::createNullValue() : agentc::createStringValue(envelopeState));
    agentc::addNamedItem(status, "expect", jsonSnapshot(expect));
    agentc::addNamedItem(status, "result", jsonSnapshot(result));

    auto fail = [&](const std::string& code, const std::string& message) {
        agentc::addNamedItem(status, "ok", statusList(false));
        agentc::addNamedItem(status, "state", agentc::createStringValue("result_error"));
        agentc::addNamedItem(status, "error", errorObject(code, message));
        return status;
    };

    if (envelopeState.empty()) {
        return fail("missing_envelope_state", "intern result validation requires envelope.state");
    }
    if (!result) {
        return fail("missing_result", "intern result validation requires envelope.result");
    }
    if (stringField(expect, "success_field").empty()) {
        return fail("missing_success_field", "intern result contract requires expect.success_field");
    }
    if (!valueTruthy(namedValue(result, "ok"))) {
        return fail("missing_success_evidence", "intern result requires result.ok success evidence");
    }
    auto evidence = namedValue(result, "evidence");
    if (!valueTruthy(evidence)) {
        return fail("missing_evidence", "intern result requires result.evidence before coordinator trust");
    }

    size_t minEvidenceCount = 0;
    if (sizeField(expect, "min_evidence_count", minEvidenceCount) && evidenceCount(evidence) < minEvidenceCount) {
        return fail("insufficient_evidence", "intern result evidence count is below expect.min_evidence_count");
    }

    const std::string minConfidence = stringField(expect, "min_confidence");
    if (!minConfidence.empty()) {
        const std::string confidence = stringField(result, "confidence");
        if (confidence.empty()) {
            return fail("missing_confidence", "intern result requires result.confidence for the requested confidence policy");
        }
        if (confidenceRank(confidence) < confidenceRank(minConfidence) || confidenceRank(minConfidence) == 0) {
            return fail("low_confidence", "intern result confidence is below expect.min_confidence");
        }
    }

    agentc::addNamedItem(status, "ok", statusList(true));
    agentc::addNamedItem(status, "state", agentc::createStringValue("result_valid"));
    agentc::addNamedItem(status, "error", agentc::createNullValue());
    return status;
}

CPtr<agentc::ListreeValue> drop(CPtr<agentc::ListreeValue> jobOrRequest) {
    const std::string jobId = jobIdFromValue(jobOrRequest);
    if (jobId.empty()) {
        return buildDropEnvelope("", "error", false, "unknown_job", "missing intern job id");
    }
    return internJobManager().drop(jobId);
}

} // namespace intern

} // namespace agentc::edict
