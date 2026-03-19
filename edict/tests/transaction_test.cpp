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
#include "../edict_compiler.h"
#include "../edict_vm.h"

using namespace agentc::edict;

namespace {

int executeSingle(EdictVM& vm, VMOpcode op) {
    BytecodeBuffer code;
    code.addOp(op);
    return vm.execute(code);
}

} // namespace

TEST(TransactionTest, RollbackRestoresStackAndDictionaryFrames) {
    EdictVM vm;

    const size_t baselineStackDepth = vm.getResourceDepth(VMRES_STACK);
    const size_t baselineDictDepth = vm.getResourceDepth(VMRES_DICT);
    const size_t baselineStackSize = vm.getStackSize();

    auto checkpoint = vm.beginTransaction();
    ASSERT_TRUE(checkpoint.valid);

    vm.pushData(agentc::createNullValue());
    ASSERT_FALSE(executeSingle(vm, VMOP_CTX_PUSH) & VM_ERROR) << vm.getError();
    vm.pushData(agentc::createStringValue("temp"));

    EXPECT_EQ(vm.getResourceDepth(VMRES_STACK), baselineStackDepth + 1);
    EXPECT_EQ(vm.getResourceDepth(VMRES_DICT), baselineDictDepth + 1);
    EXPECT_EQ(vm.getStackSize(), baselineStackSize + 1);

    ASSERT_TRUE(vm.rollbackTransaction(checkpoint));
    EXPECT_EQ(vm.getResourceDepth(VMRES_STACK), baselineStackDepth);
    EXPECT_EQ(vm.getResourceDepth(VMRES_DICT), baselineDictDepth);
    EXPECT_EQ(vm.getStackSize(), baselineStackSize);
    EXPECT_FALSE(vm.peekData());
}

TEST(TransactionTest, NestedTransactionRollbackPreservesOuterChanges) {
    EdictVM vm;

    auto outer = vm.beginTransaction();
    ASSERT_TRUE(outer.valid);
    vm.pushData(agentc::createStringValue("outer"));

    auto inner = vm.beginTransaction();
    ASSERT_TRUE(inner.valid);
    vm.pushData(agentc::createStringValue("inner"));

    ASSERT_EQ(vm.getStackSize(), 2u);

    ASSERT_TRUE(vm.rollbackTransaction(inner));
    ASSERT_EQ(vm.getStackSize(), 1u);
    auto top = vm.peekData();
    ASSERT_TRUE(top);
    EXPECT_EQ(std::string(static_cast<char*>(top->getData()), top->getLength()), "outer");

    ASSERT_TRUE(vm.commitTransaction(outer));
}

TEST(TransactionTest, NestedTransactionCommitRequiresTopMostOrder) {
    EdictVM vm;

    auto outer = vm.beginTransaction();
    ASSERT_TRUE(outer.valid);

    auto inner = vm.beginTransaction();
    ASSERT_TRUE(inner.valid);

    EXPECT_FALSE(vm.commitTransaction(outer));
    ASSERT_TRUE(vm.commitTransaction(inner));
    ASSERT_TRUE(vm.commitTransaction(outer));
}

TEST(TransactionTest, RollbackRestoresRewriteRuleSet) {
    EdictVM vm;

    auto code = EdictCompiler().compile(R"(
        {"pattern": ["base"], "replacement": ["stable"]}
        rewrite_define ! /
    )");
    ASSERT_FALSE(vm.execute(code) & VM_ERROR) << vm.getError();
    vm.popData();

    ASSERT_EQ(vm.getRewriteRuleCount(), 1u);

    auto checkpoint = vm.beginTransaction();
    ASSERT_TRUE(checkpoint.valid);

    code = EdictCompiler().compile(R"(
        {"pattern": ["temp"], "replacement": ["transient"]}
        rewrite_define ! /
    )");
    ASSERT_FALSE(vm.execute(code) & VM_ERROR) << vm.getError();
    vm.popData();

    ASSERT_EQ(vm.getRewriteRuleCount(), 2u);

    ASSERT_TRUE(vm.rollbackTransaction(checkpoint));
    ASSERT_EQ(vm.getRewriteRuleCount(), 1u);
    EXPECT_EQ(vm.getRewriteRulePattern(0), std::vector<std::string>({"base"}));
    EXPECT_EQ(vm.getRewriteRuleReplacement(0), std::vector<std::string>({"stable"}));
    EXPECT_TRUE(vm.getRewriteRulePattern(1).empty());
}

TEST(TransactionTest, SpeculateReturnsCopiedResultAndRestoresBaseline) {
    EdictVM vm;
    vm.pushData(agentc::createStringValue("baseline"));

    CPtr<agentc::ListreeValue> result;
    std::string error;
    ASSERT_TRUE(vm.speculateValue([](EdictVM& speculativeVm) {
        auto trial = agentc::createStringValue("trial");
        speculativeVm.pushData(trial);
        return trial;
    }, result, error)) << error;
    ASSERT_TRUE(result);
    EXPECT_EQ(std::string(static_cast<char*>(result->getData()), result->getLength()), "trial");

    ASSERT_EQ(vm.getStackSize(), 1u);
    auto top = vm.peekData();
    ASSERT_TRUE(top);
    EXPECT_EQ(std::string(static_cast<char*>(top->getData()), top->getLength()), "baseline");
}

TEST(TransactionTest, SpeculateFailureRollsBackAndReturnsError) {
    EdictVM vm;
    const size_t baselineStackDepth = vm.getResourceDepth(VMRES_STACK);
    const size_t baselineDictDepth = vm.getResourceDepth(VMRES_DICT);

    CPtr<agentc::ListreeValue> result;
    std::string error;
    EXPECT_FALSE(vm.speculate([](EdictVM& speculativeVm) {
        BytecodeBuffer code = EdictCompiler().compile("swap");
        return (speculativeVm.execute(code) & VM_ERROR) == 0;
    }, result, error));
    EXPECT_FALSE(result);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(vm.getResourceDepth(VMRES_STACK), baselineStackDepth);
    EXPECT_EQ(vm.getResourceDepth(VMRES_DICT), baselineDictDepth);
    EXPECT_EQ(vm.getStackSize(), 0u);
}
