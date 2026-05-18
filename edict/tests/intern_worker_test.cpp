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

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../edict_compiler.h"
#include "../edict_vm.h"
#include "../../listree/listree.h"

using agentc::ListreeValue;
using agentc::edict::EdictCompiler;
using agentc::edict::EdictVM;
using namespace agentc::edict;

namespace {

CPtr<ListreeValue> namedValue(CPtr<ListreeValue> value, const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string textValue(CPtr<ListreeValue> value) {
    if (!value || !value->getData()) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

std::vector<std::string> listStrings(CPtr<ListreeValue> value) {
    std::vector<std::string> out;
    if (!value || !value->isListMode()) {
        return out;
    }
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            out.push_back(textValue(ref->getValue()));
        }
    });
    return out;
}

size_t sizeText(CPtr<ListreeValue> value) {
    const std::string text = textValue(value);
    if (text.empty()) {
        return 0;
    }
    return static_cast<size_t>(std::stoull(text));
}

size_t listCount(CPtr<ListreeValue> value) {
    size_t count = 0;
    if (!value || !value->isListMode()) {
        return count;
    }
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            ++count;
        }
    });
    return count;
}

bool eventListContainsKind(CPtr<ListreeValue> events, const std::string& kind) {
    if (!events || !events->isListMode()) {
        return false;
    }
    bool found = false;
    events->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (found || !ref || !ref->getValue()) {
            return;
        }
        if (textValue(namedValue(ref->getValue(), "kind")) == kind) {
            found = true;
        }
    });
    return found;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string moduleBackedInternPrelude() {
    const auto edictSourceDir = std::filesystem::path(TEST_EDICT_SOURCE_DIR);
    const auto repoRoot = edictSourceDir.parent_path();
    const auto libedict = std::filesystem::path(TEST_EDICT_BIN_DIR) / "libedict.so";
    const auto primitiveHeader = edictSourceDir / "agentc_worker_primitives.h";
    const auto workerModule = repoRoot / "cpp-agent" / "edict" / "modules" / "worker.edict";
    const auto internModule = repoRoot / "cpp-agent" / "edict" / "modules" / "intern.edict";

    return std::string("[") + libedict.string() + "] [" + primitiveHeader.string() + "] resolver.import! @workerffi\n" +
           readTextFile(workerModule) + "\n" +
           readTextFile(internModule) + "\n";
}

} // namespace

TEST(InternWorkerTest, InternRunDispatchesWorkerAndCollectsStructuredResult) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("demo-intern"));
    agentc::addNamedItem(task, "program", agentc::createStringValue(
        "'mutated @context.fact "
        "context.fact @observed "
        "/context.fact "
        "'private @workspace.note "
        "{} @result "
        "observed @result.observed "
        "workspace.note @result.workspace_note "
        "input.label @result.input_label"));

    auto context = agentc::fromJson(R"({"fact":"alpha"})");
    auto input = agentc::fromJson(R"({"label":"beta"})");
    ASSERT_TRUE(context);
    ASSERT_TRUE(input);
    agentc::addNamedItem(task, "context", context);
    agentc::addNamedItem(task, "input", input);

    vm.pushData(task);
    const int state = vm.execute(compiler.compile("intern_run! @intern_result"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto envelope = namedValue(coordinatorRoot, "intern_result");
    ASSERT_TRUE(envelope);
    EXPECT_EQ(textValue(namedValue(envelope, "task_id")), "demo-intern");
    EXPECT_EQ(textValue(namedValue(envelope, "state")), "complete");
    EXPECT_EQ(textValue(namedValue(envelope, "worker")), "edict-thread");
    EXPECT_EQ(listStrings(namedValue(envelope, "ok")), std::vector<std::string>({"ok"}));

    auto result = namedValue(envelope, "result");
    ASSERT_TRUE(result);
    EXPECT_EQ(textValue(namedValue(result, "observed")), "alpha");
    EXPECT_EQ(textValue(namedValue(result, "workspace_note")), "private");
    EXPECT_EQ(textValue(namedValue(result, "input_label")), "beta");

    auto safety = namedValue(envelope, "safety");
    ASSERT_TRUE(safety);
    EXPECT_EQ(textValue(namedValue(safety, "context_read_only")), "true");
    EXPECT_EQ(textValue(namedValue(safety, "imports_read_only")), "true");
    EXPECT_EQ(textValue(namedValue(safety, "input_snapshot")), "json");
    EXPECT_EQ(textValue(namedValue(safety, "result_merge_thread")), "coordinator");

    // The worker attempted to assign and remove `context.fact`, but G091
    // dispatch freezes the shared context before launching the worker. Both
    // writes are refused by the read-only Listree guard and the coordinator-owned
    // context remains unchanged.
    auto fact = namedValue(context, "fact");
    ASSERT_TRUE(fact);
    EXPECT_TRUE(context->isReadOnly());
    EXPECT_TRUE(fact->isReadOnly());
    EXPECT_EQ(textValue(fact), "alpha");

    // The private input snapshot may be read in the worker without freezing the
    // coordinator-owned input object.
    EXPECT_FALSE(input->isReadOnly());
    EXPECT_EQ(textValue(namedValue(input, "label")), "beta");
}

TEST(InternWorkerTest, ModuleBackedInternWordsUseImportedWorkerPrimitives) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;

    int state = vm.execute(compiler.compile(moduleBackedInternPrelude()));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto runTask = agentc::createNullValue();
    agentc::addNamedItem(runTask, "task_id", agentc::createStringValue("ffi-run-demo"));
    agentc::addNamedItem(runTask, "program", agentc::createStringValue("input.label @result.label 'run @result.mode"));
    agentc::addNamedItem(runTask, "input", agentc::fromJson(R"({"label":"delta"})"));

    vm.pushData(runTask);
    state = vm.execute(compiler.compile("intern_run! @run_result"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto runResult = namedValue(coordinatorRoot, "run_result");
    ASSERT_TRUE(runResult);
    EXPECT_EQ(textValue(namedValue(runResult, "state")), "complete");
    EXPECT_EQ(textValue(namedValue(namedValue(runResult, "result"), "label")), "delta");
    EXPECT_EQ(textValue(namedValue(namedValue(runResult, "result"), "mode")), "run");

    auto prepTask = agentc::createNullValue();
    auto prepInput = agentc::fromJson(R"({"label":"zeta"})");
    auto prepContext = agentc::fromJson(R"({"fact":"frozen"})");
    ASSERT_TRUE(prepInput);
    ASSERT_TRUE(prepContext);
    agentc::addNamedItem(prepTask, "task_id", agentc::createStringValue("ffi-prepare-demo"));
    agentc::addNamedItem(prepTask, "program", agentc::createStringValue("input.label @result.label context.fact @result.fact"));
    agentc::addNamedItem(prepTask, "input", prepInput);
    agentc::addNamedItem(prepTask, "context", prepContext);
    agentc::addNamedItem(prepTask, "max_active_jobs", agentc::createStringValue("8"));
    vm.pushData(prepTask);
    state = vm.execute(compiler.compile("worker.edict_prepare_task! @prepared prepared worker.edict_check_capacity! @prepared_capacity prepared worker.edict_capacity_status! @prepared_capacity_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto prepared = namedValue(coordinatorRoot, "prepared");
    ASSERT_TRUE(prepared);
    EXPECT_EQ(textValue(namedValue(prepared, "state")), "prepared");
    EXPECT_EQ(textValue(namedValue(prepared, "task_id")), "ffi-prepare-demo");
    EXPECT_EQ(textValue(namedValue(prepared, "max_active_jobs")), "8");
    EXPECT_TRUE(namedValue(prepared, "context")->isReadOnly());
    EXPECT_FALSE(prepInput->isReadOnly());
    agentc::addNamedItem(prepInput, "label", agentc::createStringValue("changed-after-prepare"));
    EXPECT_EQ(textValue(namedValue(namedValue(prepared, "input"), "label")), "zeta");
    auto preparedCapacity = namedValue(coordinatorRoot, "prepared_capacity");
    ASSERT_TRUE(preparedCapacity);
    EXPECT_EQ(textValue(namedValue(preparedCapacity, "state")), "capacity_ok");
    EXPECT_EQ(listStrings(namedValue(preparedCapacity, "ok")), std::vector<std::string>({"ok"}));
    auto preparedCapacityStatus = namedValue(coordinatorRoot, "prepared_capacity_status");
    ASSERT_TRUE(preparedCapacityStatus);
    EXPECT_EQ(textValue(namedValue(preparedCapacityStatus, "state")), "capacity");
    EXPECT_EQ(listStrings(namedValue(preparedCapacityStatus, "allowed")), std::vector<std::string>({"ok"}));

    vm.pushData(prepared);
    state = vm.execute(compiler.compile("worker.edict_run_prepared! @prepared_run_result"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto preparedRunResult = namedValue(coordinatorRoot, "prepared_run_result");
    ASSERT_TRUE(preparedRunResult);
    EXPECT_EQ(textValue(namedValue(preparedRunResult, "state")), "complete");
    EXPECT_EQ(textValue(namedValue(namedValue(preparedRunResult, "result"), "label")), "zeta");
    EXPECT_EQ(textValue(namedValue(namedValue(preparedRunResult, "result"), "fact")), "frozen");

    state = vm.execute(compiler.compile("worker.edict_active_count! @bp_active_before"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    const size_t bpActiveBefore = sizeText(namedValue(coordinatorRoot, "bp_active_before"));
    auto bpTask = agentc::createNullValue();
    agentc::addNamedItem(bpTask, "task_id", agentc::createStringValue("ffi-module-backpressure"));
    agentc::addNamedItem(bpTask, "program", agentc::createStringValue("'unused @result.value"));
    agentc::addNamedItem(bpTask, "max_active_jobs", agentc::createStringValue("0"));
    vm.pushData(bpTask);
    state = vm.execute(compiler.compile("intern_start! @module_backpressure worker.edict_active_count! @bp_active_after"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto moduleBackpressure = namedValue(coordinatorRoot, "module_backpressure");
    ASSERT_TRUE(moduleBackpressure);
    EXPECT_EQ(textValue(namedValue(moduleBackpressure, "state")), "backpressure");
    EXPECT_TRUE(eventListContainsKind(namedValue(moduleBackpressure, "events"), "backpressure"));
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "bp_active_after")), bpActiveBefore);

    auto asyncTask = agentc::createNullValue();
    agentc::addNamedItem(asyncTask, "task_id", agentc::createStringValue("ffi-async-demo"));
    agentc::addNamedItem(asyncTask, "program", agentc::createStringValue("input.label @result.label 'async @result.mode"));
    agentc::addNamedItem(asyncTask, "input", agentc::fromJson(R"({"label":"epsilon"})"));

    vm.pushData(asyncTask);
    state = vm.execute(compiler.compile("intern_start! @job"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto job = namedValue(coordinatorRoot, "job");
    ASSERT_TRUE(job);
    const std::string jobId = textValue(namedValue(job, "job_id"));
    ASSERT_FALSE(jobId.empty());

    CPtr<ListreeValue> status;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(jobId));
        state = vm.execute(compiler.compile("intern_sync! @status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        status = namedValue(coordinatorRoot, "status");
        ASSERT_TRUE(status);
        if (textValue(namedValue(status, "state")) == "complete") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(status);
    EXPECT_EQ(textValue(namedValue(status, "state")), "complete");
    EXPECT_EQ(textValue(namedValue(namedValue(status, "result"), "label")), "epsilon");
    EXPECT_EQ(textValue(namedValue(namedValue(status, "result"), "mode")), "async");
    EXPECT_GE(listCount(namedValue(status, "events")), 1u);

    state = vm.execute(compiler.compile("worker.edict_active_count! @active_before"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    const size_t activeBefore = sizeText(namedValue(coordinatorRoot, "active_before"));

    auto rawTask = agentc::createNullValue();
    agentc::addNamedItem(rawTask, "task_id", agentc::createStringValue("ffi-raw-demo"));
    agentc::addNamedItem(rawTask, "program", agentc::createStringValue("'raw @result.mode"));
    vm.pushData(rawTask);
    state = vm.execute(compiler.compile("worker.edict_start! @raw_job worker.edict_active_count! @active_mid"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto rawJob = namedValue(coordinatorRoot, "raw_job");
    ASSERT_TRUE(rawJob);
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "active_mid")), activeBefore + 1);
    const std::string rawJobId = textValue(namedValue(rawJob, "job_id"));
    ASSERT_FALSE(rawJobId.empty());

    CPtr<ListreeValue> rawStatus;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(rawJobId));
        state = vm.execute(compiler.compile("worker.edict_drain_events! @raw_events raw_job.job_id raw_events worker.edict_collect! @raw_status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        rawStatus = namedValue(coordinatorRoot, "raw_status");
        ASSERT_TRUE(rawStatus);
        if (textValue(namedValue(rawStatus, "state")) == "complete") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(rawStatus);
    EXPECT_EQ(textValue(namedValue(rawStatus, "state")), "complete");
    EXPECT_EQ(textValue(namedValue(namedValue(rawStatus, "result"), "mode")), "raw");
    EXPECT_GE(listCount(namedValue(rawStatus, "events")), 1u);
    state = vm.execute(compiler.compile("worker.edict_active_count! @active_after"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "active_after")), activeBefore);

    auto cancelTask = agentc::createNullValue();
    agentc::addNamedItem(cancelTask, "task_id", agentc::createStringValue("ffi-cancel-demo"));
    agentc::addNamedItem(cancelTask, "program", agentc::createStringValue("'should-not-merge @result.value"));
    vm.pushData(cancelTask);
    state = vm.execute(compiler.compile("worker.edict_start! @cancel_job cancel_job.job_id worker.edict_request_cancel! @cancel_events"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto cancelJob = namedValue(coordinatorRoot, "cancel_job");
    ASSERT_TRUE(cancelJob);
    const std::string cancelJobId = textValue(namedValue(cancelJob, "job_id"));
    ASSERT_FALSE(cancelJobId.empty());
    EXPECT_TRUE(eventListContainsKind(namedValue(coordinatorRoot, "cancel_events"), "cancelled"));

    CPtr<ListreeValue> cancelledStatus;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(cancelJobId));
        state = vm.execute(compiler.compile("cancel_events worker.edict_collect! @cancelled_status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        cancelledStatus = namedValue(coordinatorRoot, "cancelled_status");
        ASSERT_TRUE(cancelledStatus);
        if (textValue(namedValue(cancelledStatus, "state")) == "cancelled") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(cancelledStatus);
    EXPECT_EQ(textValue(namedValue(cancelledStatus, "state")), "cancelled");
    EXPECT_TRUE(eventListContainsKind(namedValue(cancelledStatus, "events"), "cancelled"));

    state = vm.execute(compiler.compile("worker.edict_active_count! @drop_active_before"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    const size_t dropActiveBefore = sizeText(namedValue(coordinatorRoot, "drop_active_before"));
    auto dropTask = agentc::createNullValue();
    agentc::addNamedItem(dropTask, "task_id", agentc::createStringValue("ffi-drop-demo"));
    agentc::addNamedItem(dropTask, "program", agentc::createStringValue("'dropped @result.value"));
    vm.pushData(dropTask);
    state = vm.execute(compiler.compile("worker.edict_start! @drop_job worker.edict_active_count! @drop_active_mid"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto dropJob = namedValue(coordinatorRoot, "drop_job");
    ASSERT_TRUE(dropJob);
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "drop_active_mid")), dropActiveBefore + 1);
    const std::string dropJobId = textValue(namedValue(dropJob, "job_id"));
    ASSERT_FALSE(dropJobId.empty());
    vm.pushData(agentc::createStringValue(dropJobId));
    state = vm.execute(compiler.compile("worker.edict_drop! @drop_status worker.edict_active_count! @drop_active_after"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    EXPECT_EQ(textValue(namedValue(namedValue(coordinatorRoot, "drop_status"), "state")), "dropped");
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "drop_active_after")), dropActiveBefore);
    vm.pushData(agentc::createStringValue(dropJobId));
    state = vm.execute(compiler.compile("worker.edict_sync! @dropped_sync"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    EXPECT_EQ(textValue(namedValue(namedValue(namedValue(coordinatorRoot, "dropped_sync"), "error"), "code")), "unknown_job");

    vm.pushData(agentc::createStringValue("missing-status-job"));
    vm.pushData(agentc::createListValue());
    state = vm.execute(compiler.compile("worker.edict_collect_status! @missing_status missing_status intern.sync_envelope! @missing_status_envelope"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto missingStatus = namedValue(coordinatorRoot, "missing_status");
    ASSERT_TRUE(missingStatus);
    EXPECT_EQ(textValue(namedValue(missingStatus, "kind")), "worker_collect_status");
    EXPECT_TRUE(listStrings(namedValue(missingStatus, "found")).empty());
    EXPECT_FALSE(namedValue(missingStatus, "state"));
    auto missingStatusEnvelope = namedValue(coordinatorRoot, "missing_status_envelope");
    ASSERT_TRUE(missingStatusEnvelope);
    EXPECT_EQ(textValue(namedValue(missingStatusEnvelope, "state")), "error");
    EXPECT_EQ(textValue(namedValue(namedValue(missingStatusEnvelope, "error"), "code")), "unknown_job");

    vm.pushData(agentc::createStringValue("missing-module-job"));
    state = vm.execute(compiler.compile("intern_cancel! @cancel_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto cancelStatus = namedValue(coordinatorRoot, "cancel_status");
    ASSERT_TRUE(cancelStatus);
    EXPECT_EQ(textValue(namedValue(cancelStatus, "state")), "error");
    EXPECT_EQ(textValue(namedValue(namedValue(cancelStatus, "error"), "code")), "unknown_job");
}

TEST(InternWorkerTest, InternStartAndSyncCollectsStructuredResultAsynchronously) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("async-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue(
        "input.label @result.label "
        "'done @result.status"));
    agentc::addNamedItem(task, "input", agentc::fromJson(R"({"label":"gamma"})"));

    vm.pushData(task);
    int state = vm.execute(compiler.compile("intern_start! @job"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto job = namedValue(coordinatorRoot, "job");
    ASSERT_TRUE(job);
    const std::string jobId = textValue(namedValue(job, "job_id"));
    ASSERT_FALSE(jobId.empty());
    EXPECT_EQ(textValue(namedValue(job, "state")), "started");
    EXPECT_EQ(textValue(namedValue(job, "worker")), "edict-thread-async");
    ASSERT_TRUE(namedValue(job, "waitable"));
    EXPECT_EQ(textValue(namedValue(namedValue(job, "waitable"), "kind")), "root1-broker");

    CPtr<ListreeValue> status;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(jobId));
        state = vm.execute(compiler.compile("intern_sync! @status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        status = namedValue(coordinatorRoot, "status");
        ASSERT_TRUE(status);
        if (textValue(namedValue(status, "state")) == "complete") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(status);
    EXPECT_EQ(textValue(namedValue(status, "job_id")), jobId);
    EXPECT_EQ(textValue(namedValue(status, "state")), "complete");
    EXPECT_EQ(textValue(namedValue(status, "worker")), "edict-thread-async");
    EXPECT_EQ(listStrings(namedValue(status, "ok")), std::vector<std::string>({"ok"}));

    auto result = namedValue(status, "result");
    ASSERT_TRUE(result);
    EXPECT_EQ(textValue(namedValue(result, "label")), "gamma");
    EXPECT_EQ(textValue(namedValue(result, "status")), "done");

    auto events = namedValue(status, "events");
    ASSERT_TRUE(events);
    EXPECT_TRUE(events->isListMode());
    EXPECT_GE(listCount(events), 1u);
}

TEST(InternWorkerTest, InternStartReportsBackpressureWhenActiveLimitIsReached) {
    EdictVM vm;
    EdictCompiler compiler;

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("backpressure-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue("'unused @result.value"));
    agentc::addNamedItem(task, "max_active_jobs", agentc::createStringValue("0"));

    vm.pushData(task);
    const int state = vm.execute(compiler.compile("intern_start!"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto envelope = vm.popData();
    ASSERT_TRUE(envelope);
    EXPECT_TRUE(listStrings(namedValue(envelope, "ok")).empty());
    EXPECT_EQ(textValue(namedValue(envelope, "state")), "backpressure");
    auto error = namedValue(envelope, "error");
    ASSERT_TRUE(error);
    EXPECT_EQ(textValue(namedValue(error, "code")), "backpressure");
    EXPECT_TRUE(eventListContainsKind(namedValue(envelope, "events"), "backpressure"));
}

TEST(InternWorkerTest, InternCancelRequestsCancellationAndFinalSyncReportsCancelled) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("cancel-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue("'should-not-merge @result.value"));

    vm.pushData(task);
    int state = vm.execute(compiler.compile("intern_start! @job"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto job = namedValue(coordinatorRoot, "job");
    ASSERT_TRUE(job);
    const std::string jobId = textValue(namedValue(job, "job_id"));
    ASSERT_FALSE(jobId.empty());

    vm.pushData(agentc::createStringValue(jobId));
    state = vm.execute(compiler.compile("intern_cancel! @cancel_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto cancelStatus = namedValue(coordinatorRoot, "cancel_status");
    ASSERT_TRUE(cancelStatus);
    EXPECT_EQ(textValue(namedValue(cancelStatus, "state")), "cancel_requested");
    EXPECT_EQ(listStrings(namedValue(cancelStatus, "ok")), std::vector<std::string>({"ok"}));
    EXPECT_TRUE(eventListContainsKind(namedValue(cancelStatus, "events"), "cancelled"));

    CPtr<ListreeValue> status;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(jobId));
        state = vm.execute(compiler.compile("intern_sync! @status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        status = namedValue(coordinatorRoot, "status");
        ASSERT_TRUE(status);
        if (textValue(namedValue(status, "state")) == "cancelled") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(status);
    EXPECT_EQ(textValue(namedValue(status, "job_id")), jobId);
    EXPECT_EQ(textValue(namedValue(status, "state")), "cancelled");
    EXPECT_TRUE(listStrings(namedValue(status, "ok")).empty());
    auto error = namedValue(status, "error");
    ASSERT_TRUE(error);
    EXPECT_EQ(textValue(namedValue(error, "code")), "cancelled");
    EXPECT_TRUE(eventListContainsKind(namedValue(status, "events"), "cancelled"));
}

TEST(InternWorkerTest, InternSyncReportsUnknownJobAsStructuredError) {
    EdictVM vm;
    EdictCompiler compiler;

    vm.pushData(agentc::createStringValue("missing-job"));
    const int state = vm.execute(compiler.compile("intern_sync!"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto envelope = vm.popData();
    ASSERT_TRUE(envelope);
    EXPECT_TRUE(listStrings(namedValue(envelope, "ok")).empty());
    EXPECT_EQ(textValue(namedValue(envelope, "job_id")), "missing-job");
    EXPECT_EQ(textValue(namedValue(envelope, "state")), "error");
    auto error = namedValue(envelope, "error");
    ASSERT_TRUE(error);
    EXPECT_EQ(textValue(namedValue(error, "code")), "unknown_job");
}

TEST(InternWorkerTest, InternCancelReportsUnknownJobAsStructuredError) {
    EdictVM vm;
    EdictCompiler compiler;

    vm.pushData(agentc::createStringValue("missing-job"));
    const int state = vm.execute(compiler.compile("intern_cancel!"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto envelope = vm.popData();
    ASSERT_TRUE(envelope);
    EXPECT_TRUE(listStrings(namedValue(envelope, "ok")).empty());
    EXPECT_EQ(textValue(namedValue(envelope, "job_id")), "missing-job");
    EXPECT_EQ(textValue(namedValue(envelope, "state")), "error");
    auto error = namedValue(envelope, "error");
    ASSERT_TRUE(error);
    EXPECT_EQ(textValue(namedValue(error, "code")), "unknown_job");
}

TEST(InternWorkerTest, InternRunReportsMissingProgramAsStructuredError) {
    EdictVM vm;
    EdictCompiler compiler;

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("bad-intern"));
    vm.pushData(task);

    const int state = vm.execute(compiler.compile("intern_run!"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto envelope = vm.popData();
    ASSERT_TRUE(envelope);
    EXPECT_TRUE(listStrings(namedValue(envelope, "ok")).empty());
    EXPECT_EQ(textValue(namedValue(envelope, "task_id")), "bad-intern");
    EXPECT_EQ(textValue(namedValue(envelope, "state")), "error");

    auto error = namedValue(envelope, "error");
    ASSERT_TRUE(error);
    EXPECT_EQ(textValue(namedValue(error, "code")), "missing_program");
}
