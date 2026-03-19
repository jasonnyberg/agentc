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
#include "../edict_vm.h"

using namespace agentc::edict;

namespace {

std::string asText(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return "";
    }

    return std::string(static_cast<char*>(value->getData()), value->getLength());
}

BytecodeBuffer pushStrings(const std::vector<std::string>& values) {
    BytecodeBuffer code;
    for (const auto& value : values) {
        code.addOp(VMOP_PUSHEXT);
        code.addValue(Value(value));
    }
    return code;
}

} // namespace

TEST(RewriteRuntimeTest, ExactSuffixMatchRewritesStack) {
    EdictVM vm;
    vm.addRewriteRule({"x"}, {"w"});

    ASSERT_FALSE(vm.execute(pushStrings({"x"})) & VM_ERROR) << vm.getError();
    EXPECT_EQ(asText(vm.popData()), "w");
}

TEST(RewriteRuntimeTest, LongestMatchWins) {
    EdictVM vm;
    vm.addRewriteRule({"x"}, {"short"});
    vm.addRewriteRule({"y", "x"}, {"long"});

    ASSERT_FALSE(vm.execute(pushStrings({"y", "x"})) & VM_ERROR) << vm.getError();
    EXPECT_EQ(asText(vm.popData()), "long");
}

TEST(RewriteRuntimeTest, WildcardCaptureSubstitutesMatchedValues) {
    EdictVM vm;
    vm.addRewriteRule({"$1", "x"}, {"x", "$1"});

    ASSERT_FALSE(vm.execute(pushStrings({"tea", "x"})) & VM_ERROR) << vm.getError();
    EXPECT_EQ(asText(vm.popData()), "tea");
    EXPECT_EQ(asText(vm.popData()), "x");
}

TEST(RewriteRuntimeTest, EqualLengthTieUsesEarlierRule) {
    EdictVM vm;
    vm.addRewriteRule({"x"}, {"first"});
    vm.addRewriteRule({"x"}, {"second"});

    ASSERT_FALSE(vm.execute(pushStrings({"x"})) & VM_ERROR) << vm.getError();
    EXPECT_EQ(asText(vm.popData()), "first");
}

TEST(RewriteRuntimeTest, RewriteLoopBudgetPreventsInfiniteRewrite) {
    EdictVM vm;
    vm.addRewriteRule({"x"}, {"x"});

    ASSERT_FALSE(vm.execute(pushStrings({"x"})) & VM_ERROR) << vm.getError();
    EXPECT_EQ(asText(vm.popData()), "x");
}

TEST(RewriteRuntimeTest, NoMatchLeavesStackUntouched) {
    EdictVM vm;
    vm.addRewriteRule({"x"}, {"w"});

    ASSERT_FALSE(vm.execute(pushStrings({"tea"})) & VM_ERROR) << vm.getError();
    EXPECT_EQ(asText(vm.popData()), "tea");
}

TEST(RewriteRuntimeTest, TypeAwareListPatternMatchesListValues) {
    EdictVM vm;
    vm.addRewriteRule({"#list", "x"}, {"list-seen"});
    vm.pushData(agentc::createListValue());

    ASSERT_FALSE(vm.execute(pushStrings({"x"})) & VM_ERROR) << vm.getError();
    EXPECT_EQ(asText(vm.popData()), "list-seen");
}

TEST(RewriteRuntimeTest, ManualModeSkipsAutomaticRewriteUntilApplied) {
    EdictVM vm;
    vm.addRewriteRule({"x"}, {"manual-hit"});
    vm.setRewriteMode(EdictVM::RewriteMode::Manual);

    ASSERT_FALSE(vm.execute(pushStrings({"x"})) & VM_ERROR) << vm.getError();
    EXPECT_EQ(asText(vm.popData()), "x");

    vm.pushData(agentc::createStringValue("x"));
    BytecodeBuffer apply;
    apply.addOp(VMOP_REWRITE_APPLY);
    ASSERT_FALSE(vm.execute(apply) & VM_ERROR) << vm.getError();
    auto trace = vm.popData();
    ASSERT_TRUE(trace);
    EXPECT_EQ(asText(trace->find("status")->getValue(false, false)), "matched");
    EXPECT_EQ(asText(vm.popData()), "manual-hit");
}
