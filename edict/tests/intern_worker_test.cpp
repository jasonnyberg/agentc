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
