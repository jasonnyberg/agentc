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

#include <string>
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

    // The worker attempted to assign `context.fact`, but G091 dispatch freezes
    // the shared context before launching the worker. The write is refused by
    // the read-only Listree guard and the coordinator-owned context remains
    // unchanged.
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
