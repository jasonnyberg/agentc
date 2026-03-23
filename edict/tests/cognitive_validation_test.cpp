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

#include <filesystem>
#include <fstream>

#include "../../cartographer/parser.h"
#include "../../cartographer/resolver.h"
#include "../edict_compiler.h"
#include "../edict_vm.h"

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

using namespace agentc::edict;

namespace {

void executeScript(EdictVM& vm, const std::string& script) {
    static const std::string logicPrelude = []() {
        const std::filesystem::path buildDir(TEST_BUILD_DIR);
        const std::filesystem::path rootBuildDir = buildDir.parent_path();
        const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
        const std::filesystem::path libPath = rootBuildDir / "kanren" / "libkanren.so";
        const std::filesystem::path headerPath = sourceDir / "kanren_runtime_ffi_poc.h";
        const std::filesystem::path resolvedPath = buildDir / "cognitive_validation_kanren_runtime_ffi.json";

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

    auto code = EdictCompiler().compile(logicPrelude + script);
    ASSERT_FALSE(vm.execute(code) & VM_ERROR) << vm.getError();
}

} // namespace

TEST(CognitiveValidationTest, TransactionRollbackContainsLogicAndRewriteExecution) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["dup", "dot", "sqrt"], "replacement": ["magnitude"]}
        rewrite_define ! /
    )");
    vm.popData();

    const size_t baselineStackDepth = vm.getResourceDepth(VMRES_STACK);
    const size_t baselineDictDepth = vm.getResourceDepth(VMRES_DICT);
    const size_t baselineStackSize = vm.getStackSize();

    auto checkpoint = vm.beginTransaction();
    ASSERT_TRUE(checkpoint.valid);

    executeScript(vm, R"(
        {"fresh": ["q"],
         "where": [["membero", "q", ["tea", "cake"]]],
         "results": ["q"]}
        logic_run !
    )");

    auto logicResult = vm.popData();
    ASSERT_TRUE(logicResult);
    ASSERT_TRUE(logicResult->isListMode());

    executeScript(vm, R"(
        'dup 'dot 'sqrt
    )");

    auto rewriteResult = vm.popData();
    ASSERT_TRUE(rewriteResult);
    ASSERT_TRUE(rewriteResult->getData());
    EXPECT_EQ(std::string(static_cast<char*>(rewriteResult->getData()), rewriteResult->getLength()), "magnitude");

    ASSERT_TRUE(vm.rollbackTransaction(checkpoint));
    EXPECT_EQ(vm.getResourceDepth(VMRES_STACK), baselineStackDepth);
    EXPECT_EQ(vm.getResourceDepth(VMRES_DICT), baselineDictDepth);
    EXPECT_EQ(vm.getStackSize(), baselineStackSize);
}

TEST(CognitiveValidationTest, TransactionRollbackRemovesTransientRewriteRules) {
    EdictVM vm;

    executeScript(vm, R"(
        {"pattern": ["base"], "replacement": ["stable"]}
        rewrite_define ! /
    )");
    vm.popData();
    ASSERT_EQ(vm.getRewriteRuleCount(), 1u);

    auto checkpoint = vm.beginTransaction();
    ASSERT_TRUE(checkpoint.valid);

    executeScript(vm, R"(
        {"pattern": ["temp"], "replacement": ["transient"]}
        rewrite_define ! /
    )");
    vm.popData();
    ASSERT_EQ(vm.getRewriteRuleCount(), 2u);

    ASSERT_TRUE(vm.rollbackTransaction(checkpoint));
    ASSERT_EQ(vm.getRewriteRuleCount(), 1u);

    executeScript(vm, R"(
        'temp
    )");

    auto result = vm.popData();
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->getData());
    EXPECT_EQ(std::string(static_cast<char*>(result->getData()), result->getLength()), "temp");
}
