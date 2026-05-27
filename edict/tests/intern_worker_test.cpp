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
#include "../static_declaration_image.h"
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

void addInternStartContract(CPtr<ListreeValue> task) {
    auto expect = agentc::createNullValue();
    agentc::addNamedItem(expect, "success_field", agentc::createStringValue("ok"));
    agentc::addNamedItem(task, "expect", expect);

    auto limits = agentc::createNullValue();
    agentc::addNamedItem(limits, "max_result_bytes", agentc::createStringValue("65536"));
    agentc::addNamedItem(task, "limits", limits);
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

void loadModuleBackedIntern(EdictVM& vm, EdictCompiler& compiler) {
    const int state = vm.execute(compiler.compile(moduleBackedInternPrelude()));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
}

} // namespace

TEST(InternWorkerTest, InternWordsAreModuleBackedNotBootstrapBuiltins) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);
    (void)vm;

    EXPECT_FALSE(root->find("intern_run"));
    EXPECT_FALSE(root->find("intern_start"));
    EXPECT_FALSE(root->find("intern_sync"));
    EXPECT_FALSE(root->find("intern_cancel"));
}

TEST(InternWorkerTest, InternRunDispatchesWorkerAndCollectsStructuredResult) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

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

TEST(InternWorkerTest, WorkerExecutesSharedStaticBaseCodeThunk) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    auto context = agentc::createNullValue();
    EdictVM contextVm(context);
    int state = contextVm.execute(compiler.compile(
        "'shared-fact @fact "
        "[ input.label @result.label context.fact @result.fact 'shared-base @result.mode ] freeze! @base"));
    ASSERT_FALSE(state & VM_ERROR) << contextVm.getError();

    auto base = namedValue(context, "base");
    ASSERT_TRUE(base);
    ASSERT_TRUE(base->isReadOnly());
    context->setReadOnly(true);

    const SlabId contextSid = context.getSlabId();
    const SlabId baseSid = base.getSlabId();
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().markSlotStaticImmortal(contextSid));
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().markSlotStaticImmortal(baseSid));
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(contextSid));
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(baseSid));

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("shared-base-code-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue("context.base!"));
    agentc::addNamedItem(task, "context", context);
    agentc::addNamedItem(task, "input", agentc::fromJson(R"({"label":"worker-input"})"));
    addInternStartContract(task);

    vm.pushData(task);
    state = vm.execute(compiler.compile("intern_start! @job"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto job = namedValue(coordinatorRoot, "job");
    ASSERT_TRUE(job);
    EXPECT_EQ(textValue(namedValue(job, "state")), "started");
    const std::string jobId = textValue(namedValue(job, "job_id"));
    ASSERT_FALSE(jobId.empty());

    CPtr<ListreeValue> status;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(jobId));
        state = vm.execute(compiler.compile("intern_sync! @shared_base_status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        status = namedValue(coordinatorRoot, "shared_base_status");
        ASSERT_TRUE(status);
        if (textValue(namedValue(status, "state")) == "complete") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(status);
    EXPECT_EQ(textValue(namedValue(status, "state")), "complete");
    EXPECT_EQ(listStrings(namedValue(status, "ok")), std::vector<std::string>({"ok"}));
    auto result = namedValue(status, "result");
    ASSERT_TRUE(result);
    EXPECT_EQ(textValue(namedValue(result, "label")), "worker-input");
    EXPECT_EQ(textValue(namedValue(result, "fact")), "shared-fact");
    EXPECT_EQ(textValue(namedValue(result, "mode")), "shared-base");
    EXPECT_TRUE(context->isReadOnly());
    EXPECT_TRUE(base->isReadOnly());
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(contextSid));
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(baseSid));

    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().unmarkSlotStaticImmortal(baseSid));
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().unmarkSlotStaticImmortal(contextSid));
    EXPECT_FALSE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(contextSid));
    EXPECT_FALSE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(baseSid));
}

TEST(InternWorkerTest, WorkerExecutesSharedBaseWithMmapStaticMountContract) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    auto image = agentc::edict::static_image::buildWorkerPrimitiveDeclarationImage();
    const auto path = std::filesystem::temp_directory_path() /
                      "agentc-worker-static-mount-contract-test.acsdi";
    std::string error;
    ASSERT_TRUE(agentc::edict::static_image::writeDeclarationImageContainer(image, path.string(), &error)) << error;
    auto restored = agentc::edict::static_image::readDeclarationImageContainerMmapReadOnly(path.string(), &error);
    ASSERT_TRUE(restored) << error;
    auto mounted = agentc::edict::static_image::mountDeclarationImageReadOnly(restored);
    ASSERT_TRUE(mounted.validation.ok) << mounted.validation.code << ": " << mounted.validation.message;
    ASSERT_TRUE(mounted.root);
    EXPECT_TRUE(mounted.root->isReadOnly());
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().slotIsStaticImmortal(mounted.rootId));

    auto manifest = namedValue(mounted.root, "manifest");
    ASSERT_TRUE(manifest);
    auto staticMounts = agentc::createNullValue();
    auto baseMount = agentc::createNullValue();
    const std::string module = textValue(namedValue(manifest, "module"));
    const std::string payloadHash = textValue(namedValue(manifest, "payload_hash"));
    agentc::addNamedItem(baseMount, "source", agentc::createStringValue("g103-read-only-mmap-container"));
    agentc::addNamedItem(baseMount, "image_id", agentc::createStringValue(module + ":" + payloadHash));
    agentc::addNamedItem(baseMount, "manifest_hash", agentc::createStringValue(payloadHash));
    agentc::addNamedItem(baseMount, "root_descriptor", agentc::createStringValue(textValue(namedValue(manifest, "root_id"))));
    agentc::addNamedItem(baseMount, "root_slot", agentc::createStringValue(
        std::to_string(mounted.rootId.first) + ":" + std::to_string(mounted.rootId.second)));
    agentc::addNamedItem(baseMount, "contains_native_handles", agentc::createStringValue(
        textValue(namedValue(manifest, "contains_native_handles"))));
    agentc::addNamedItem(staticMounts, "base", baseMount);

    auto context = agentc::createNullValue();
    EdictVM contextVm(context);
    int state = contextVm.execute(compiler.compile(
        "'shared-fact @fact "
        "[ input.label @result.label context.fact @result.fact 'shared-base @result.mode ] freeze! @base"));
    ASSERT_FALSE(state & VM_ERROR) << contextVm.getError();
    auto base = namedValue(context, "base");
    ASSERT_TRUE(base);
    ASSERT_TRUE(base->isReadOnly());
    context->setReadOnly(true);
    const SlabId contextSid = context.getSlabId();
    const SlabId baseSid = base.getSlabId();
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().markSlotStaticImmortal(contextSid));
    ASSERT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().markSlotStaticImmortal(baseSid));

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("mmap-static-mount-contract-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue(
        "context.base! "
        "static_mounts.base.source @result.mount_source "
        "static_mounts.base.image_id @result.image_id "
        "static_mounts.base.root_descriptor @result.root_descriptor "
        "static_mounts.base.contains_native_handles @result.contains_native_handles"));
    agentc::addNamedItem(task, "context", context);
    agentc::addNamedItem(task, "static_mounts", staticMounts);
    agentc::addNamedItem(task, "input", agentc::fromJson(R"({"label":"worker-input"})"));
    addInternStartContract(task);

    vm.pushData(task);
    state = vm.execute(compiler.compile("intern_start! @job"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto job = namedValue(coordinatorRoot, "job");
    ASSERT_TRUE(job);
    EXPECT_EQ(textValue(namedValue(job, "state")), "started");
    const std::string jobId = textValue(namedValue(job, "job_id"));
    ASSERT_FALSE(jobId.empty());

    CPtr<ListreeValue> status;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(jobId));
        state = vm.execute(compiler.compile("intern_sync! @mmap_static_status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        status = namedValue(coordinatorRoot, "mmap_static_status");
        ASSERT_TRUE(status);
        if (textValue(namedValue(status, "state")) == "complete") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(status);
    EXPECT_EQ(textValue(namedValue(status, "state")), "complete");
    EXPECT_EQ(listStrings(namedValue(status, "ok")), std::vector<std::string>({"ok"}));
    auto safety = namedValue(status, "safety");
    ASSERT_TRUE(safety);
    EXPECT_EQ(textValue(namedValue(safety, "static_mounts_read_only")), "true");
    auto result = namedValue(status, "result");
    ASSERT_TRUE(result);
    EXPECT_EQ(textValue(namedValue(result, "label")), "worker-input");
    EXPECT_EQ(textValue(namedValue(result, "fact")), "shared-fact");
    EXPECT_EQ(textValue(namedValue(result, "mode")), "shared-base");
    EXPECT_EQ(textValue(namedValue(result, "mount_source")), "g103-read-only-mmap-container");
    EXPECT_EQ(textValue(namedValue(result, "image_id")), module + ":" + payloadHash);
    EXPECT_EQ(textValue(namedValue(result, "root_descriptor")), "worker.edict/declarations");
    EXPECT_EQ(textValue(namedValue(result, "contains_native_handles")), "false");

    EXPECT_TRUE(staticMounts->isReadOnly());
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().unmarkSlotStaticImmortal(baseSid));
    EXPECT_TRUE(Allocator<agentc::ListreeValue>::getAllocator().unmarkSlotStaticImmortal(contextSid));
    for (const auto& sid : mounted.staticValueSlots) {
        (void)Allocator<agentc::ListreeValue>::getAllocator().unmarkSlotStaticImmortal(sid);
    }
    std::filesystem::remove(path);
}

TEST(InternWorkerTest, ModuleBackedInternWordsUseImportedWorkerPrimitives) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    int state = VM_NORMAL;

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
    state = vm.execute(compiler.compile("worker.edict_prepare_task! @prepared prepared worker.edict_capacity_status! @prepared_capacity_status"));
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
    auto preparedCapacityStatus = namedValue(coordinatorRoot, "prepared_capacity_status");
    ASSERT_TRUE(preparedCapacityStatus);
    EXPECT_EQ(textValue(namedValue(preparedCapacityStatus, "state")), "capacity");
    EXPECT_EQ(listStrings(namedValue(preparedCapacityStatus, "allowed")), std::vector<std::string>({"ok"}));

    vm.pushData(prepared);
    state = vm.execute(compiler.compile("worker.edict_run_status_prepared! @prepared_run_status prepared_run_status intern.run_envelope! @prepared_run_result"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto preparedRunStatus = namedValue(coordinatorRoot, "prepared_run_status");
    ASSERT_TRUE(preparedRunStatus);
    EXPECT_EQ(textValue(namedValue(preparedRunStatus, "kind")), "worker_run_status");
    EXPECT_FALSE(namedValue(preparedRunStatus, "state"));
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
    addInternStartContract(bpTask);
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
    addInternStartContract(asyncTask);
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
    addInternStartContract(rawTask);
    vm.pushData(rawTask);
    state = vm.execute(compiler.compile("worker.edict_start_status! @raw_job_status raw_job_status intern.start_envelope! @raw_job worker.edict_active_count! @active_mid"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto rawJobStatus = namedValue(coordinatorRoot, "raw_job_status");
    ASSERT_TRUE(rawJobStatus);
    EXPECT_EQ(textValue(namedValue(rawJobStatus, "kind")), "worker_start_status");
    EXPECT_FALSE(namedValue(rawJobStatus, "state"));
    auto rawJob = namedValue(coordinatorRoot, "raw_job");
    ASSERT_TRUE(rawJob);
    EXPECT_LE(sizeText(namedValue(coordinatorRoot, "active_mid")), activeBefore + 1);
    const std::string rawJobId = textValue(namedValue(rawJob, "job_id"));
    ASSERT_FALSE(rawJobId.empty());

    CPtr<ListreeValue> rawStatus;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(rawJobId));
        state = vm.execute(compiler.compile("worker.edict_drain_events! @raw_events raw_job.job_id raw_events worker.edict_collect_status! @raw_status_facts raw_status_facts intern.sync_envelope! @raw_status"));
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
    addInternStartContract(cancelTask);
    vm.pushData(cancelTask);
    state = vm.execute(compiler.compile("worker.edict_start_status! @cancel_job_status cancel_job_status intern.start_envelope! @cancel_job cancel_job.job_id worker.edict_request_cancel! @cancel_events"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto cancelJob = namedValue(coordinatorRoot, "cancel_job");
    ASSERT_TRUE(cancelJob);
    const std::string cancelJobId = textValue(namedValue(cancelJob, "job_id"));
    ASSERT_FALSE(cancelJobId.empty());
    const bool cancelAccepted = eventListContainsKind(namedValue(coordinatorRoot, "cancel_events"), "cancelled");

    CPtr<ListreeValue> cancelledStatus;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(cancelJobId));
        state = vm.execute(compiler.compile("cancel_events worker.edict_collect_status! @cancelled_status_facts cancelled_status_facts intern.sync_envelope! @cancelled_status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        cancelledStatus = namedValue(coordinatorRoot, "cancelled_status");
        ASSERT_TRUE(cancelledStatus);
        const std::string stateText = textValue(namedValue(cancelledStatus, "state"));
        if (stateText == "cancelled" || stateText == "complete") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(cancelledStatus);
    if (cancelAccepted) {
        EXPECT_EQ(textValue(namedValue(cancelledStatus, "state")), "cancelled");
        EXPECT_TRUE(eventListContainsKind(namedValue(cancelledStatus, "events"), "cancelled"));
    } else {
        EXPECT_EQ(textValue(namedValue(cancelledStatus, "state")), "complete");
    }

    state = vm.execute(compiler.compile("worker.edict_active_count! @drop_active_before"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    const size_t dropActiveBefore = sizeText(namedValue(coordinatorRoot, "drop_active_before"));
    auto dropTask = agentc::createNullValue();
    agentc::addNamedItem(dropTask, "task_id", agentc::createStringValue("ffi-drop-demo"));
    agentc::addNamedItem(dropTask, "program", agentc::createStringValue("'dropped @result.value"));
    addInternStartContract(dropTask);
    vm.pushData(dropTask);
    state = vm.execute(compiler.compile("worker.edict_start_status! @drop_job_status drop_job_status intern.start_envelope! @drop_job worker.edict_active_count! @drop_active_mid"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto dropJob = namedValue(coordinatorRoot, "drop_job");
    ASSERT_TRUE(dropJob);
    EXPECT_LE(sizeText(namedValue(coordinatorRoot, "drop_active_mid")), dropActiveBefore + 1);
    const std::string dropJobId = textValue(namedValue(dropJob, "job_id"));
    ASSERT_FALSE(dropJobId.empty());
    vm.pushData(agentc::createStringValue(dropJobId));
    state = vm.execute(compiler.compile("worker.edict_drop! @drop_status worker.edict_active_count! @drop_active_after"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    const std::string dropState = textValue(namedValue(namedValue(coordinatorRoot, "drop_status"), "state"));
    EXPECT_TRUE(dropState == "abandoned" || dropState == "dropped");
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "drop_active_after")), dropActiveBefore);
    vm.pushData(agentc::createStringValue(dropJobId));
    state = vm.execute(compiler.compile("worker.edict_drain_events! @dropped_events drop_job.job_id dropped_events worker.edict_collect_status! @dropped_sync_facts dropped_sync_facts intern.sync_envelope! @dropped_sync"));
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
    loadModuleBackedIntern(vm, compiler);

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("async-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue(
        "input.label @result.label "
        "'done @result.status"));
    addInternStartContract(task);
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

TEST(InternWorkerTest, InternContractValidatorsCheckTaskAndStatusShape) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    auto validTask = agentc::createNullValue();
    agentc::addNamedItem(validTask, "task_id", agentc::createStringValue("contract-demo"));
    agentc::addNamedItem(validTask, "program", agentc::createStringValue("'ok @result.value"));
    agentc::addNamedItem(validTask, "limits", agentc::fromJson(R"({"timeout_ms":"1000","max_result_bytes":"65536"})"));
    agentc::addNamedItem(validTask, "expect", agentc::fromJson(R"({"success_field":"ok","result_shape":"object"})"));

    vm.pushData(validTask);
    int state = vm.execute(compiler.compile("intern.validate_task_contract! @contract_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto contractStatus = namedValue(coordinatorRoot, "contract_status");
    ASSERT_TRUE(contractStatus);
    EXPECT_EQ(textValue(namedValue(contractStatus, "state")), "contract_valid");
    EXPECT_EQ(listStrings(namedValue(contractStatus, "ok")), std::vector<std::string>({"ok"}));
    EXPECT_EQ(textValue(namedValue(contractStatus, "task_id")), "contract-demo");

    auto invalidTask = agentc::createNullValue();
    agentc::addNamedItem(invalidTask, "task_id", agentc::createStringValue("bad-contract"));
    agentc::addNamedItem(invalidTask, "program", agentc::createStringValue(""));
    agentc::addNamedItem(invalidTask, "expect", agentc::fromJson(R"({"success_field":"ok"})"));
    vm.pushData(invalidTask);
    state = vm.execute(compiler.compile("intern.validate_task_contract! @bad_contract_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto badStatus = namedValue(coordinatorRoot, "bad_contract_status");
    ASSERT_TRUE(badStatus);
    EXPECT_EQ(textValue(namedValue(badStatus, "state")), "contract_error");
    EXPECT_EQ(textValue(namedValue(namedValue(badStatus, "error"), "code")), "missing_program");

    auto envelope = agentc::createNullValue();
    agentc::addNamedItem(envelope, "state", agentc::createStringValue("complete"));
    agentc::addNamedItem(envelope, "task_id", agentc::createStringValue("contract-demo"));
    agentc::addNamedItem(envelope, "publication", agentc::createNullValue());
    vm.pushData(envelope);
    state = vm.execute(compiler.compile("intern.validate_status_envelope! @envelope_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto envelopeStatus = namedValue(coordinatorRoot, "envelope_status");
    ASSERT_TRUE(envelopeStatus);
    EXPECT_EQ(textValue(namedValue(envelopeStatus, "state")), "envelope_valid");
    EXPECT_EQ(textValue(namedValue(envelopeStatus, "envelope_state")), "complete");
}

TEST(InternWorkerTest, InternExamplesRepresentSafeTaskClasses) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    int state = vm.execute(compiler.compile(
        "intern.example_gather_task! @gather "
        "gather intern.validate_task_contract! @gather_contract "
        "intern.example_classify_task! @classify "
        "classify intern.validate_task_contract! @classify_contract "
        "intern.example_filter_task! @filter "
        "filter intern.validate_task_contract! @filter_contract"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto gather = namedValue(coordinatorRoot, "gather");
    ASSERT_TRUE(gather);
    EXPECT_EQ(textValue(namedValue(gather, "task_class")), "gather");
    EXPECT_EQ(textValue(namedValue(namedValue(coordinatorRoot, "gather_contract"), "state")), "contract_valid");

    auto classify = namedValue(coordinatorRoot, "classify");
    ASSERT_TRUE(classify);
    EXPECT_EQ(textValue(namedValue(classify, "task_class")), "classify");
    EXPECT_EQ(textValue(namedValue(namedValue(coordinatorRoot, "classify_contract"), "state")), "contract_valid");

    auto filter = namedValue(coordinatorRoot, "filter");
    ASSERT_TRUE(filter);
    EXPECT_EQ(textValue(namedValue(filter, "task_class")), "filter");
    EXPECT_EQ(textValue(namedValue(namedValue(coordinatorRoot, "filter_contract"), "state")), "contract_valid");
}

TEST(InternWorkerTest, InternResultContractRequiresSuccessAndEvidence) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    auto validCheck = agentc::fromJson(R"({
        "expect": {"result_shape":"object", "success_field":"ok", "evidence_field":"evidence"},
        "envelope": {
            "state":"complete",
            "task_id":"result-contract-valid",
            "result": {"ok":"ok", "summary":"bounded", "evidence":["file.cpp:12"], "confidence":"high"}
        }
    })");
    vm.pushData(validCheck);
    int state = vm.execute(compiler.compile("intern.validate_result_contract! @valid_result_contract"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto validStatus = namedValue(coordinatorRoot, "valid_result_contract");
    ASSERT_TRUE(validStatus);
    EXPECT_EQ(textValue(namedValue(validStatus, "state")), "result_valid");
    EXPECT_EQ(listStrings(namedValue(validStatus, "ok")), std::vector<std::string>({"ok"}));

    auto missingEvidence = agentc::fromJson(R"({
        "expect": {"result_shape":"object", "success_field":"ok", "evidence_field":"evidence"},
        "envelope": {
            "state":"complete",
            "task_id":"result-contract-missing-evidence",
            "result": {"ok":"ok", "summary":"unsupported"}
        }
    })");
    vm.pushData(missingEvidence);
    state = vm.execute(compiler.compile("intern.validate_result_contract! @missing_evidence_contract"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto missingEvidenceStatus = namedValue(coordinatorRoot, "missing_evidence_contract");
    ASSERT_TRUE(missingEvidenceStatus);
    EXPECT_EQ(textValue(namedValue(missingEvidenceStatus, "state")), "result_error");
    EXPECT_EQ(textValue(namedValue(namedValue(missingEvidenceStatus, "error"), "code")), "missing_evidence");

    auto missingSuccess = agentc::fromJson(R"({
        "expect": {"result_shape":"object", "success_field":"ok", "evidence_field":"evidence"},
        "envelope": {
            "state":"complete",
            "task_id":"result-contract-missing-success",
            "result": {"summary":"unsupported", "evidence":["file.cpp:12"]}
        }
    })");
    vm.pushData(missingSuccess);
    state = vm.execute(compiler.compile("intern.validate_result_contract! @missing_success_contract"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto missingSuccessStatus = namedValue(coordinatorRoot, "missing_success_contract");
    ASSERT_TRUE(missingSuccessStatus);
    EXPECT_EQ(textValue(namedValue(missingSuccessStatus, "state")), "result_error");
    EXPECT_EQ(textValue(namedValue(namedValue(missingSuccessStatus, "error"), "code")), "missing_success_evidence");
}

TEST(InternWorkerTest, InternResultContractAppliesConfidenceAndEvidenceThresholds) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    auto expect = agentc::fromJson(R"({
        "result_shape":"object",
        "success_field":"ok",
        "evidence_field":"evidence",
        "min_evidence_count":"2",
        "min_confidence":"medium"
    })");
    auto trustedEnvelope = agentc::fromJson(R"({
        "state":"complete",
        "task_id":"threshold-valid",
        "result": {"ok":"ok", "evidence":["file.cpp:12", "test.log:3"], "confidence":"high"}
    })");
    vm.pushData(expect);
    vm.pushData(trustedEnvelope);
    int state = vm.execute(compiler.compile("intern.validate_trusted_result! @trusted_result_contract"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto trustedStatus = namedValue(coordinatorRoot, "trusted_result_contract");
    ASSERT_TRUE(trustedStatus);
    EXPECT_EQ(textValue(namedValue(trustedStatus, "state")), "result_valid");

    auto lowConfidence = agentc::fromJson(R"({
        "expect": {
            "result_shape":"object",
            "success_field":"ok",
            "evidence_field":"evidence",
            "min_evidence_count":"1",
            "min_confidence":"medium"
        },
        "envelope": {
            "state":"complete",
            "task_id":"threshold-low-confidence",
            "result": {"ok":"ok", "evidence":["file.cpp:12"], "confidence":"low"}
        }
    })");
    vm.pushData(lowConfidence);
    state = vm.execute(compiler.compile("intern.validate_result_contract! @low_confidence_contract"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto lowConfidenceStatus = namedValue(coordinatorRoot, "low_confidence_contract");
    ASSERT_TRUE(lowConfidenceStatus);
    EXPECT_EQ(textValue(namedValue(lowConfidenceStatus, "state")), "result_error");
    EXPECT_EQ(textValue(namedValue(namedValue(lowConfidenceStatus, "error"), "code")), "low_confidence");

    auto missingConfidence = agentc::fromJson(R"({
        "expect": {
            "result_shape":"object",
            "success_field":"ok",
            "evidence_field":"evidence",
            "min_evidence_count":"1",
            "min_confidence":"medium"
        },
        "envelope": {
            "state":"complete",
            "task_id":"threshold-missing-confidence",
            "result": {"ok":"ok", "evidence":["file.cpp:12"]}
        }
    })");
    vm.pushData(missingConfidence);
    state = vm.execute(compiler.compile("intern.validate_result_contract! @missing_confidence_contract"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto missingConfidenceStatus = namedValue(coordinatorRoot, "missing_confidence_contract");
    ASSERT_TRUE(missingConfidenceStatus);
    EXPECT_EQ(textValue(namedValue(missingConfidenceStatus, "state")), "result_error");
    EXPECT_EQ(textValue(namedValue(namedValue(missingConfidenceStatus, "error"), "code")), "missing_confidence");

    auto insufficientEvidence = agentc::fromJson(R"({
        "expect": {
            "result_shape":"object",
            "success_field":"ok",
            "evidence_field":"evidence",
            "min_evidence_count":"2",
            "min_confidence":"low"
        },
        "envelope": {
            "state":"complete",
            "task_id":"threshold-insufficient-evidence",
            "result": {"ok":"ok", "evidence":["file.cpp:12"], "confidence":"high"}
        }
    })");
    vm.pushData(insufficientEvidence);
    state = vm.execute(compiler.compile("intern.validate_result_contract! @insufficient_evidence_contract"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto insufficientEvidenceStatus = namedValue(coordinatorRoot, "insufficient_evidence_contract");
    ASSERT_TRUE(insufficientEvidenceStatus);
    EXPECT_EQ(textValue(namedValue(insufficientEvidenceStatus, "state")), "result_error");
    EXPECT_EQ(textValue(namedValue(namedValue(insufficientEvidenceStatus, "error"), "code")), "insufficient_evidence");
}

TEST(InternWorkerTest, InternStartEnforcesTaskContractBeforeDispatch) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    auto missingExpect = agentc::createNullValue();
    agentc::addNamedItem(missingExpect, "task_id", agentc::createStringValue("missing-expect"));
    agentc::addNamedItem(missingExpect, "program", agentc::createStringValue("'unused @result.value"));
    auto limits = agentc::createNullValue();
    agentc::addNamedItem(limits, "max_result_bytes", agentc::createStringValue("65536"));
    agentc::addNamedItem(missingExpect, "limits", limits);

    vm.pushData(missingExpect);
    int state = vm.execute(compiler.compile("intern_start! @missing_expect_status worker.edict_active_count! @active_after_missing_expect"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto missingExpectStatus = namedValue(coordinatorRoot, "missing_expect_status");
    ASSERT_TRUE(missingExpectStatus);
    EXPECT_EQ(textValue(namedValue(missingExpectStatus, "state")), "error");
    EXPECT_EQ(textValue(namedValue(namedValue(missingExpectStatus, "error"), "code")), "missing_success_field");
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "active_after_missing_expect")), 0u);

    auto overBroad = agentc::createNullValue();
    agentc::addNamedItem(overBroad, "task_id", agentc::createStringValue("over-broad"));
    agentc::addNamedItem(overBroad, "program", agentc::createStringValue("'unused @result.value"));
    auto expect = agentc::createNullValue();
    agentc::addNamedItem(expect, "success_field", agentc::createStringValue("ok"));
    agentc::addNamedItem(overBroad, "expect", expect);

    vm.pushData(overBroad);
    state = vm.execute(compiler.compile("intern_start! @over_broad_status worker.edict_active_count! @active_after_over_broad"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto overBroadStatus = namedValue(coordinatorRoot, "over_broad_status");
    ASSERT_TRUE(overBroadStatus);
    EXPECT_EQ(textValue(namedValue(overBroadStatus, "state")), "error");
    EXPECT_EQ(textValue(namedValue(namedValue(overBroadStatus, "error"), "code")), "missing_result_limit");
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "active_after_over_broad")), 0u);
}

TEST(InternWorkerTest, WorkerLifecycleStatusTracksAbandonedRunningJobs) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    int state = vm.execute(compiler.compile("worker.edict_lifecycle_status! @lifecycle_before"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto before = namedValue(coordinatorRoot, "lifecycle_before");
    ASSERT_TRUE(before);
    const size_t abandonedBefore = sizeText(namedValue(before, "total_abandoned_jobs"));

    std::string program;
    for (int i = 0; i < 2000; ++i) {
        program += "yield! ";
    }
    program += "'finished @result.value";

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("lifecycle-abandon-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue(program));
    addInternStartContract(task);

    vm.pushData(task);
    state = vm.execute(compiler.compile("intern_start! @job"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto job = namedValue(coordinatorRoot, "job");
    ASSERT_TRUE(job);
    EXPECT_EQ(textValue(namedValue(job, "state")), "started");
    const std::string jobId = textValue(namedValue(job, "job_id"));
    ASSERT_FALSE(jobId.empty());

    vm.pushData(agentc::createStringValue(jobId));
    state = vm.execute(compiler.compile("worker.edict_drop! @drop_status worker.edict_lifecycle_status! @lifecycle_after_drop"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto dropStatus = namedValue(coordinatorRoot, "drop_status");
    ASSERT_TRUE(dropStatus);
    EXPECT_EQ(textValue(namedValue(dropStatus, "state")), "abandoned");

    auto afterDrop = namedValue(coordinatorRoot, "lifecycle_after_drop");
    ASSERT_TRUE(afterDrop);
    EXPECT_EQ(textValue(namedValue(afterDrop, "kind")), "worker_lifecycle_status");
    EXPECT_GE(sizeText(namedValue(afterDrop, "total_abandoned_jobs")), abandonedBefore + 1);
    EXPECT_GE(sizeText(namedValue(afterDrop, "total_started_jobs")), 1u);
}

TEST(InternWorkerTest, TerminalAsyncJobsAreRetainedUntilExplicitDrop) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("retention-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue("'retained @result.value"));
    addInternStartContract(task);

    vm.pushData(task);
    int state = vm.execute(compiler.compile("intern_start! @job"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    const std::string jobId = textValue(namedValue(namedValue(coordinatorRoot, "job"), "job_id"));
    ASSERT_FALSE(jobId.empty());

    CPtr<ListreeValue> firstStatus;
    for (int i = 0; i < 100; ++i) {
        vm.pushData(agentc::createStringValue(jobId));
        state = vm.execute(compiler.compile("intern_sync! @first_status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        firstStatus = namedValue(coordinatorRoot, "first_status");
        ASSERT_TRUE(firstStatus);
        if (textValue(namedValue(firstStatus, "state")) == "complete") {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(firstStatus);
    EXPECT_EQ(textValue(namedValue(firstStatus, "state")), "complete");
    EXPECT_EQ(textValue(namedValue(namedValue(firstStatus, "result"), "value")), "retained");

    state = vm.execute(compiler.compile("worker.edict_active_count! @active_after_terminal"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    EXPECT_EQ(sizeText(namedValue(coordinatorRoot, "active_after_terminal")), 0u);

    vm.pushData(agentc::createStringValue(jobId));
    state = vm.execute(compiler.compile("intern_sync! @second_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto secondStatus = namedValue(coordinatorRoot, "second_status");
    ASSERT_TRUE(secondStatus);
    EXPECT_EQ(textValue(namedValue(secondStatus, "state")), "complete");
    EXPECT_EQ(textValue(namedValue(namedValue(secondStatus, "result"), "value")), "retained");

    vm.pushData(agentc::createStringValue(jobId));
    state = vm.execute(compiler.compile("worker.edict_drop! @drop_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    EXPECT_EQ(textValue(namedValue(namedValue(coordinatorRoot, "drop_status"), "state")), "dropped");

    vm.pushData(agentc::createStringValue(jobId));
    state = vm.execute(compiler.compile("intern_sync! @after_drop_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto afterDropStatus = namedValue(coordinatorRoot, "after_drop_status");
    ASSERT_TRUE(afterDropStatus);
    EXPECT_EQ(textValue(namedValue(afterDropStatus, "state")), "error");
    EXPECT_EQ(textValue(namedValue(namedValue(afterDropStatus, "error"), "code")), "unknown_job");
}

TEST(InternWorkerTest, InternStartReportsBackpressureWhenActiveLimitIsReached) {
    EdictVM vm;
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("backpressure-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue("'unused @result.value"));
    addInternStartContract(task);
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
    loadModuleBackedIntern(vm, compiler);

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("cancel-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue("'should-not-merge @result.value"));
    addInternStartContract(task);

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
    const std::string cancelState = textValue(namedValue(cancelStatus, "state"));
    EXPECT_TRUE(cancelState == "cancel_requested" || cancelState == "cancelled" || cancelState == "complete");

    if (cancelState == "complete") {
        // G091 lifecycle cleanup defines cancellation as a non-retroactive,
        // cooperative request: if the detached worker reaches a terminal state
        // before the request is observed, intern_cancel! returns the terminal
        // result instead of suppressing it as cancelled.
        EXPECT_FALSE(eventListContainsKind(namedValue(cancelStatus, "events"), "cancelled"));
        EXPECT_EQ(listStrings(namedValue(cancelStatus, "ok")), std::vector<std::string>({"ok"}));
        return;
    }

    EXPECT_TRUE(eventListContainsKind(namedValue(cancelStatus, "events"), "cancelled"));
    if (cancelState == "cancel_requested") {
        EXPECT_EQ(listStrings(namedValue(cancelStatus, "ok")), std::vector<std::string>({"ok"}));
    }

    CPtr<ListreeValue> status = cancelState == "cancelled" ? cancelStatus : nullptr;
    for (int i = 0; !status && i < 100; ++i) {
        vm.pushData(agentc::createStringValue(jobId));
        state = vm.execute(compiler.compile("intern_sync! @status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        status = namedValue(coordinatorRoot, "status");
        ASSERT_TRUE(status);
        if (textValue(namedValue(status, "state")) == "cancelled") {
            break;
        }
        status = nullptr;
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

TEST(InternWorkerTest, WorkerYieldCheckpointsObserveAsyncCancellation) {
    auto coordinatorRoot = agentc::createNullValue();
    EdictVM vm(coordinatorRoot);
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

    std::string checkpointProgram;
    for (int i = 0; i < 5000; ++i) {
        checkpointProgram += "yield! ";
    }
    checkpointProgram += "'should-not-merge @result.value";

    auto task = agentc::createNullValue();
    agentc::addNamedItem(task, "task_id", agentc::createStringValue("checkpoint-cancel-demo"));
    agentc::addNamedItem(task, "program", agentc::createStringValue(checkpointProgram));
    addInternStartContract(task);

    vm.pushData(task);
    int state = vm.execute(compiler.compile("intern_start! @job"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    const std::string jobId = textValue(namedValue(namedValue(coordinatorRoot, "job"), "job_id"));
    ASSERT_FALSE(jobId.empty());

    vm.pushData(agentc::createStringValue(jobId));
    state = vm.execute(compiler.compile("intern_cancel! @cancel_status"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    CPtr<ListreeValue> status = namedValue(coordinatorRoot, "cancel_status");
    ASSERT_TRUE(status);
    for (int i = 0; textValue(namedValue(status, "state")) != "cancelled" && i < 100; ++i) {
        vm.pushData(agentc::createStringValue(jobId));
        state = vm.execute(compiler.compile("intern_sync! @checkpoint_status"));
        ASSERT_FALSE(state & VM_ERROR) << vm.getError();
        status = namedValue(coordinatorRoot, "checkpoint_status");
        ASSERT_TRUE(status);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    ASSERT_TRUE(status);
    EXPECT_EQ(textValue(namedValue(status, "state")), "cancelled");
    EXPECT_TRUE(listStrings(namedValue(status, "ok")).empty());
    EXPECT_TRUE(eventListContainsKind(namedValue(status, "events"), "cancelled"));
    EXPECT_EQ(textValue(namedValue(namedValue(status, "error"), "code")), "cancelled");
    EXPECT_FALSE(namedValue(namedValue(status, "result"), "value"));
}

TEST(InternWorkerTest, InternSyncReportsUnknownJobAsStructuredError) {
    EdictVM vm;
    EdictCompiler compiler;
    loadModuleBackedIntern(vm, compiler);

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
    loadModuleBackedIntern(vm, compiler);

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
    loadModuleBackedIntern(vm, compiler);

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
