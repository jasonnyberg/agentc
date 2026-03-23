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

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "../../cartographer/parser.h"
#include "../../cartographer/resolver.h"
#include "../edict_compiler.h"
#include "../../kanren/logic_evaluator.h"
#include "../edict_vm.h"

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

using namespace agentc::edict;

namespace {

std::string asText(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return "";
    }

    return std::string(static_cast<char*>(value->getData()), value->getLength());
}

std::vector<std::string> listToStrings(CPtr<agentc::ListreeValue> value) {
    std::vector<std::string> out;
    if (!value || !value->isListMode()) {
        return out;
    }

    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (!ref || !ref->getValue() || !ref->getValue()->getData()) {
            return;
        }
        out.emplace_back(static_cast<char*>(ref->getValue()->getData()), ref->getValue()->getLength());
    });
    std::reverse(out.begin(), out.end());
    return out;
}

std::vector<CPtr<agentc::ListreeValue>> listItems(CPtr<agentc::ListreeValue> value) {
    std::vector<CPtr<agentc::ListreeValue>> out;
    if (!value || !value->isListMode()) {
        return out;
    }

    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            out.push_back(ref->getValue());
        }
    });
    std::reverse(out.begin(), out.end());
    return out;
}

CPtr<agentc::ListreeValue> runScript(const std::string& script) {
    EdictVM vm;
    static const std::string prelude = []() {
        const std::filesystem::path buildDir(TEST_BUILD_DIR);
        const std::filesystem::path rootBuildDir = buildDir.parent_path();
        const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
        const std::filesystem::path libPath = rootBuildDir / "kanren" / "libkanren.so";
        const std::filesystem::path headerPath = sourceDir / "kanren_runtime_ffi_poc.h";
        const std::filesystem::path resolvedPath = buildDir / "logic_surface_kanren_runtime_ffi.json";

        agentc::cartographer::Mapper mapper;
        agentc::cartographer::Mapper::ParseDescription description;
        std::string error;
        EXPECT_TRUE(agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) << error;

        agentc::cartographer::resolver::ResolvedApi resolved;
        EXPECT_TRUE(agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) << error;

        std::ofstream output(resolvedPath);
        EXPECT_TRUE(output.good());
        output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
        output.close();

        return std::string("[") + resolvedPath.string() + "] resolver.import_resolved ! @logicffi logicffi.agentc_logic_eval_ltv @logic logic @logic_run ";
    }();

    auto code = EdictCompiler().compile(prelude + script);
    EXPECT_FALSE(vm.execute(code) & VM_ERROR) << vm.getError();
    return vm.popData();
}

CPtr<agentc::ListreeValue> buildValue(const std::string& script) {
    EdictVM vm;
    auto code = EdictCompiler().compile(script);
    EXPECT_FALSE(vm.execute(code) & VM_ERROR) << vm.getError();
    return vm.popData();
}

} // namespace

TEST(LogicSurfaceTest, FreshAndEqualProducesSingleResult) {
    auto result = runScript(R"(
        {"fresh": ["q"],
         "where": [["==", "q", "tea"]],
         "results": ["q"]}
        logic_run !
    )");

    auto values = listToStrings(result);
    ASSERT_EQ(values.size(), 1u);
    EXPECT_EQ(values[0], "tea");
}

TEST(LogicSurfaceTest, CondeProducesMultipleResults) {
    auto result = runScript(R"(
        {"fresh": ["q"],
         "conde": [
           [["==", "q", "tea"]],
           [["==", "q", "coffee"]]
         ],
         "results": ["q"]}
        logic_run !
    )");

    auto values = listToStrings(result);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], "tea");
    EXPECT_EQ(values[1], "coffee");
}

TEST(LogicSurfaceTest, RecursiveRelationInvocationWorksFromEdict) {
    auto result = runScript(R"(
        {"fresh": ["q"],
         "where": [["membero", "q", ["tea", "cake", "jam"]]],
         "results": ["q"]}
        logic_run !
    )");

    auto values = listToStrings(result);
    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], "tea");
    EXPECT_EQ(values[1], "cake");
    EXPECT_EQ(values[2], "jam");
}

TEST(LogicSurfaceTest, ObjectSpecWorksThroughLogicEvaluatorAlias) {
    auto result = runScript(R"(
        {"fresh": ["q"],
         "where": [["membero", "q", ["tea", "cake"]]],
         "results": ["q"]}
        logic!
    )");

    auto values = listToStrings(result);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], "tea");
    EXPECT_EQ(values[1], "cake");
}

TEST(LogicSurfaceTest, LibraryEvaluatorConsumesCanonicalObjectSpec) {
    auto spec = buildValue(R"(
        {"fresh": ["q"],
         "where": [["membero", "q", ["tea", "cake"]]],
         "results": ["q"]}
    )");

    auto result = agentc::kanren::evaluateLogicSpec(spec);
    ASSERT_TRUE(result.ok) << result.error;

    auto values = listToStrings(result.value);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], "tea");
    EXPECT_EQ(values[1], "cake");
}

TEST(LogicSurfaceTest, ContradictoryQueryReturnsEmptyResults) {
    auto result = runScript(R"(
        {"fresh": ["q"],
         "where": [["==", "q", "tea"], ["==", "q", "coffee"]],
         "results": ["q"]}
        logic_run !
    )");

    auto values = listToStrings(result);
    EXPECT_TRUE(values.empty());
}

TEST(LogicSurfaceTest, ObjectSpecSupportsMultiVariableResults) {
    auto result = runScript(R"(
        {"fresh": ["head", "tail"],
         "where": [["conso", "head", "tail", ["tea", "cake"]]],
         "results": ["head", "tail"]}
        logic!
    )");

    auto answers = listItems(result);
    ASSERT_EQ(answers.size(), 1u);
    auto tupleItems = listItems(answers[0]);
    ASSERT_EQ(tupleItems.size(), 2u);
    EXPECT_EQ(asText(tupleItems[0]), "tea");
    auto tailItems = listItems(tupleItems[1]);
    ASSERT_EQ(tailItems.size(), 2u);
    EXPECT_TRUE(asText(tailItems[0]) == "cake" || asText(tailItems[1]) == "cake");
}

TEST(LogicSurfaceTest, ObjectSpecFeedsIntoSubsequentEdictWorkflow) {
    auto result = runScript(R"(
        {"fresh": ["q"],
         "where": [["membero", "q", ["tea", "cake"]]],
         "results": ["q"],
         "limit": "2"}
        logic! @answers
        answers
    )");

    auto values = listToStrings(result);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], "tea");
    EXPECT_EQ(values[1], "cake");
}
