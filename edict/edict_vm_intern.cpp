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
#include "../core/root1_resource_broker.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
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
    bool hasMaxActiveJobs = false;
    size_t maxActiveJobs = 0;
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
        ready_ = true;
    }

    InternWorkerOutcome load() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return outcome_;
    }

    bool ready() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ready_;
    }

private:
    mutable std::mutex mutex_;
    InternWorkerOutcome outcome_;
    bool ready_ = false;
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
    agentc::addNamedItem(envelope, "publication", agentc::createNullValue());

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

bool parseInternTask(CPtr<agentc::ListreeValue> task,
                     bool allowUnsafeFfiCalls,
                     const std::string& opName,
                     InternWorkerInput& input,
                     CPtr<agentc::ListreeValue>& errorEnvelope) {
    if (!task || task->isListMode()) {
        input.allowUnsafeFfiCalls = allowUnsafeFfiCalls;
        errorEnvelope = buildInternResult(input,
            {false, VM_ERROR, "null", "invalid_task", opName + " expects a task object"});
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
        errorEnvelope = buildInternResult(input,
            {false, VM_ERROR, "null", "missing_program", "intern task is missing a non-empty program string"});
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

bool descriptorsContainKind(const std::vector<agentc::root1::MailboxDescriptor>& descriptors,
                            agentc::root1::MailboxEventKind kind) {
    for (const auto& descriptor : descriptors) {
        if (descriptor.eventKind == kind) {
            return true;
        }
    }
    return false;
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

CPtr<agentc::ListreeValue> buildStartEnvelope(const InternWorkerInput& input,
                                              const std::string& jobId,
                                              agentc::root1::ParticipantId participant) {
    auto envelope = agentc::createNullValue();
    agentc::addNamedItem(envelope, "ok", statusList(true));
    agentc::addNamedItem(envelope, "state", agentc::createStringValue("started"));
    agentc::addNamedItem(envelope, "job_id", agentc::createStringValue(jobId));
    agentc::addNamedItem(envelope, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(envelope, "waitable", waitableValue(jobId, participant));
    agentc::addNamedItem(envelope, "publication", agentc::createNullValue());
    return envelope;
}

CPtr<agentc::ListreeValue> buildRunningEnvelope(const InternWorkerInput& input,
                                                const std::string& jobId,
                                                agentc::root1::ParticipantId participant,
                                                const std::vector<agentc::root1::MailboxDescriptor>& events) {
    auto envelope = agentc::createNullValue();
    agentc::addNamedItem(envelope, "ok", statusList(true));
    agentc::addNamedItem(envelope, "state", agentc::createStringValue("running"));
    agentc::addNamedItem(envelope, "job_id", agentc::createStringValue(jobId));
    agentc::addNamedItem(envelope, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(envelope, "waitable", waitableValue(jobId, participant));
    agentc::addNamedItem(envelope, "events", descriptorsToList(events));
    agentc::addNamedItem(envelope, "complete", statusList(false));
    agentc::addNamedItem(envelope, "publication", agentc::createNullValue());
    return envelope;
}

CPtr<agentc::ListreeValue> buildCancelRequestedEnvelope(const InternWorkerInput& input,
                                                       const std::string& jobId,
                                                       agentc::root1::ParticipantId participant,
                                                       const std::vector<agentc::root1::MailboxDescriptor>& events) {
    auto envelope = agentc::createNullValue();
    agentc::addNamedItem(envelope, "ok", statusList(true));
    agentc::addNamedItem(envelope, "state", agentc::createStringValue("cancel_requested"));
    agentc::addNamedItem(envelope, "job_id", agentc::createStringValue(jobId));
    agentc::addNamedItem(envelope, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(envelope, "waitable", waitableValue(jobId, participant));
    agentc::addNamedItem(envelope, "events", descriptorsToList(events));
    agentc::addNamedItem(envelope, "complete", statusList(false));
    agentc::addNamedItem(envelope, "publication", agentc::createNullValue());
    return envelope;
}

CPtr<agentc::ListreeValue> buildCancelledFinalEnvelope(const InternWorkerInput& input,
                                                       const std::string& jobId,
                                                       agentc::root1::ParticipantId participant,
                                                       const std::vector<agentc::root1::MailboxDescriptor>& events) {
    auto envelope = buildInternResult(input,
        {false, VM_NORMAL, "null", "cancelled", "intern job was cancelled before result collection"});
    agentc::addNamedItem(envelope, "state", agentc::createStringValue("cancelled"));
    agentc::addNamedItem(envelope, "job_id", agentc::createStringValue(jobId));
    agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(envelope, "waitable", waitableValue(jobId, participant));
    agentc::addNamedItem(envelope, "events", descriptorsToList(events));
    return envelope;
}

CPtr<agentc::ListreeValue> buildBackpressureEnvelope(const InternWorkerInput& input,
                                                     size_t activeJobs,
                                                     size_t maxActiveJobs) {
    auto envelope = agentc::createNullValue();
    agentc::addNamedItem(envelope, "ok", statusList(false));
    agentc::addNamedItem(envelope, "state", agentc::createStringValue("backpressure"));
    agentc::addNamedItem(envelope, "task_id", agentc::createStringValue(input.taskId));
    agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread-async"));
    agentc::addNamedItem(envelope, "job_id", agentc::createNullValue());
    agentc::addNamedItem(envelope, "waitable", agentc::createNullValue());
    agentc::addNamedItem(envelope, "publication", agentc::createNullValue());
    agentc::addNamedItem(envelope, "error", errorObject(
        "backpressure",
        "intern job limit reached: active=" + std::to_string(activeJobs) +
            " max=" + std::to_string(maxActiveJobs)));
    agentc::addNamedItem(envelope, "events", singletonDescriptorList(
        agentc::root1::MailboxEventKind::Backpressure,
        0,
        "intern job limit reached"));
    return envelope;
}

CPtr<agentc::ListreeValue> buildUnknownJobEnvelope(const std::string& jobId) {
    InternWorkerInput input;
    input.taskId = jobId;
    auto envelope = buildInternResult(input,
        {false, VM_ERROR, "null", "unknown_job", "unknown intern job id: " + jobId});
    agentc::addNamedItem(envelope, "job_id", agentc::createStringValue(jobId));
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
    std::atomic<bool> cancelRequested{false};
};

class InternJobManager {
public:
    CPtr<agentc::ListreeValue> start(InternWorkerInput input) {
        const size_t maxActiveJobs = input.hasMaxActiveJobs ? input.maxActiveJobs : defaultMaxActiveJobs_;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (jobs_.size() >= maxActiveJobs) {
                return buildBackpressureEnvelope(input, jobs_.size(), maxActiveJobs);
            }
        }

        auto job = std::make_shared<AsyncInternJob>();
        job->numericId = nextJobId_++;
        job->jobId = "intern-job-" + std::to_string(job->numericId);
        job->participant = broker_.registerParticipant();
        job->input = input;
        job->slot = std::make_shared<InternJoinSlot>();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_[job->jobId] = job;
        }

        std::thread([this, job]() {
            runInternWorker(job->input, *job->slot);
            const auto outcome = job->slot->load();
            auto descriptor = makeDescriptor(
                outcome.ok
                    ? agentc::root1::MailboxEventKind::Complete
                    : agentc::root1::MailboxEventKind::Error,
                job->numericId,
                outcome.ok ? "complete" : "error");
            broker_.sendMailboxDescriptor(job->participant, descriptor);
        }).detach();

        return buildStartEnvelope(job->input, job->jobId, job->participant);
    }

    CPtr<agentc::ListreeValue> cancel(const std::string& jobId) {
        std::shared_ptr<AsyncInternJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(jobId);
            if (it == jobs_.end()) {
                return buildUnknownJobEnvelope(jobId);
            }
            job = it->second;
        }

        const bool firstRequest = !job->cancelRequested.exchange(true);
        if (firstRequest) {
            broker_.sendCancellation(job->participant,
                                     job->numericId,
                                     "intern job cancellation requested");
        }
        std::vector<agentc::root1::MailboxDescriptor> events;
        events.push_back(makeDescriptor(agentc::root1::MailboxEventKind::Cancelled,
                                        job->numericId,
                                        "intern job cancellation requested"));
        return buildCancelRequestedEnvelope(job->input, job->jobId, job->participant, events);
    }

    CPtr<agentc::ListreeValue> sync(const std::string& jobId) {
        std::shared_ptr<AsyncInternJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = jobs_.find(jobId);
            if (it == jobs_.end()) {
                return buildUnknownJobEnvelope(jobId);
            }
            job = it->second;
        }

        broker_.pollReadyParticipants(0);
        auto events = broker_.drainMailboxDescriptors(job->participant);
        const bool ready = job->slot->ready();
        const bool cancelRequested = job->cancelRequested.load();
        if (cancelRequested && !ready) {
            return buildCancelRequestedEnvelope(job->input, job->jobId, job->participant, events);
        }
        if (!ready) {
            return buildRunningEnvelope(job->input, job->jobId, job->participant, events);
        }

        const auto outcome = job->slot->load();
        if (events.empty()) {
            events.push_back(makeDescriptor(
                cancelRequested
                    ? agentc::root1::MailboxEventKind::Cancelled
                    : (outcome.ok
                        ? agentc::root1::MailboxEventKind::Complete
                        : agentc::root1::MailboxEventKind::Error),
                job->numericId,
                cancelRequested ? "intern job cancellation requested" : (outcome.ok ? "complete" : "error")));
        }
        if (cancelRequested && !descriptorsContainKind(events, agentc::root1::MailboxEventKind::Cancelled)) {
            events.push_back(makeDescriptor(agentc::root1::MailboxEventKind::Cancelled,
                                            job->numericId,
                                            "intern job cancellation requested"));
        }

        CPtr<agentc::ListreeValue> envelope;
        if (cancelRequested) {
            envelope = buildCancelledFinalEnvelope(job->input, job->jobId, job->participant, events);
        } else {
            envelope = buildInternResult(job->input, outcome);
            agentc::addNamedItem(envelope, "job_id", agentc::createStringValue(job->jobId));
            agentc::addNamedItem(envelope, "worker", agentc::createStringValue("edict-thread-async"));
            agentc::addNamedItem(envelope, "waitable", waitableValue(job->jobId, job->participant));
            agentc::addNamedItem(envelope, "events", descriptorsToList(events));
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.erase(job->jobId);
        }
        return envelope;
    }

private:
    std::mutex mutex_;
    agentc::root1::Root1ResourceBroker broker_;
    uint64_t nextJobId_ = 1;
    size_t defaultMaxActiveJobs_ = 64;
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

void EdictVM::op_INTERN_RUN() {
    auto task = popData();
    InternWorkerInput input;
    CPtr<agentc::ListreeValue> errorEnvelope;
    if (!parseInternTask(task, getAllowUnsafeFfiCalls(), "intern_run", input, errorEnvelope)) {
        pushData(errorEnvelope);
        return;
    }

    InternJoinSlot slot;
    std::thread worker(runInternWorker, input, std::ref(slot));
    worker.join();

    const auto outcome = slot.load();
    pushData(buildInternResult(input, outcome));
}

void EdictVM::op_INTERN_START() {
    auto task = popData();
    InternWorkerInput input;
    CPtr<agentc::ListreeValue> errorEnvelope;
    if (!parseInternTask(task, getAllowUnsafeFfiCalls(), "intern_start", input, errorEnvelope)) {
        pushData(errorEnvelope);
        return;
    }
    pushData(internJobManager().start(input));
}

void EdictVM::op_INTERN_SYNC() {
    auto value = popData();
    bool cancel = false;

    std::string marker;
    if (value && !value->isListMode() && valueToString(value, marker) && marker == "cancel") {
        cancel = true;
        value = popData();
    } else if (value && !value->isListMode()) {
        const std::string action = stringField(value, "action");
        cancel = (action == "cancel" || stringField(value, "op") == "cancel");
    }

    const std::string jobId = jobIdFromValue(value);
    if (jobId.empty()) {
        pushData(buildUnknownJobEnvelope(""));
        return;
    }
    pushData(cancel ? internJobManager().cancel(jobId) : internJobManager().sync(jobId));
}

} // namespace agentc::edict
