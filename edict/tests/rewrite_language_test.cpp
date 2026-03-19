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
#include <string>
#include <vector>

#include "../edict_compiler.h"
#include "../edict_vm.h"

using namespace agentc::edict;

namespace {

void executeScript(EdictVM& vm, const std::string& script) {
    auto code = EdictCompiler().compile(script);
    EXPECT_FALSE(vm.execute(code) & VM_ERROR) << vm.getError();
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

std::string stringValue(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->getData()) {
        return {};
    }
    return std::string(static_cast<char*>(value->getData()), value->getLength());
}

} // namespace

TEST(RewriteLanguageTest, SourceDefinedRuleRegistersSuccessfully) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["x"], "replacement": ["w"]}
        rewrite_define !
    )");

    auto result = vm.popData();
    ASSERT_TRUE(result);
    EXPECT_TRUE(bool(result->find("pattern")));
    EXPECT_TRUE(bool(result->find("replacement")));
}

TEST(RewriteLanguageTest, MalformedRuleIsRejected) {
    EdictVM vm;
    auto code = EdictCompiler().compile(R"(
        {"pattern": ["x"], "replacement": [{}]}
        rewrite_define !
    )");

    EXPECT_TRUE(vm.execute(code) & VM_ERROR);
    EXPECT_EQ(vm.getError(), "replacement entries must be strings");
}

TEST(RewriteLanguageTest, MultipleSourceRulesPreserveRegistrationOrder) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["alpha"], "replacement": ["first"]}
        rewrite_define ! /
        {"pattern": ["beta"], "replacement": ["second"]}
        rewrite_define ! /
    )");

    ASSERT_EQ(vm.getRewriteRuleCount(), 2u);
    EXPECT_EQ(vm.getRewriteRulePattern(0), std::vector<std::string>({"alpha"}));
    EXPECT_EQ(vm.getRewriteRuleReplacement(0), std::vector<std::string>({"first"}));
    EXPECT_EQ(vm.getRewriteRulePattern(1), std::vector<std::string>({"beta"}));
    EXPECT_EQ(vm.getRewriteRuleReplacement(1), std::vector<std::string>({"second"}));
}

TEST(RewriteLanguageTest, EndToEndSourceRuleAffectsExecution) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["dup", "dot", "sqrt"], "replacement": ["magnitude"]}
        rewrite_define ! /
        "dup" "dot" "sqrt"
    )");

    auto result = vm.popData();
    ASSERT_TRUE(result);
    EXPECT_EQ(std::string(static_cast<char*>(result->getData()), result->getLength()), "magnitude");
}

TEST(RewriteLanguageTest, RewriteListReportsRegisteredRules) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["alpha"], "replacement": ["first"]}
        rewrite_define ! /
        {"pattern": ["beta", "$1"], "replacement": ["$1", "done"]}
        rewrite_define ! /
        rewrite_list !
    )");

    ASSERT_EQ(vm.getStackSize(), 1u) << vm.getError();
    auto listed = vm.popData();
    ASSERT_TRUE(listed);
    auto rules = listItems(listed);
    ASSERT_EQ(rules.size(), 2u);

    EXPECT_EQ(stringValue(rules[0]->find("index")->getValue(false, false)), "0");
    EXPECT_EQ(stringValue(listItems(rules[0]->find("pattern")->getValue(false, false))[0]), "alpha");
    EXPECT_EQ(stringValue(listItems(rules[1]->find("pattern")->getValue(false, false))[0]), "beta");
    EXPECT_EQ(stringValue(listItems(rules[1]->find("pattern")->getValue(false, false))[1]), "$1");
    EXPECT_EQ(stringValue(listItems(rules[1]->find("replacement")->getValue(false, false))[1]), "done");
}

TEST(RewriteLanguageTest, RewriteRemoveDeletesRuleByIndex) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["alpha"], "replacement": ["first"]}
        rewrite_define ! /
        {"pattern": ["beta"], "replacement": ["second"]}
        rewrite_define ! /
        "0" rewrite_remove ! /
        rewrite_list !
    )");

    auto listed = vm.popData();
    ASSERT_TRUE(listed);
    auto rules = listItems(listed);
    ASSERT_EQ(rules.size(), 1u);
    EXPECT_EQ(stringValue(listItems(rules[0]->find("pattern")->getValue(false, false))[0]), "beta");

    executeScript(vm, R"(
        "alpha"
    )");
    auto alpha = vm.popData();
    ASSERT_TRUE(alpha);
    EXPECT_EQ(stringValue(alpha), "alpha");

    executeScript(vm, R"(
        "beta"
    )");
    auto beta = vm.popData();
    ASSERT_TRUE(beta);
    EXPECT_EQ(stringValue(beta), "second");
}

TEST(RewriteLanguageTest, RewriteRemoveRejectsOutOfRangeIndex) {
    EdictVM vm;
    auto code = EdictCompiler().compile(R"(
        {"pattern": ["alpha"], "replacement": ["first"]}
        rewrite_define ! /
        "9" rewrite_remove !
    )");

    EXPECT_TRUE(vm.execute(code) & VM_ERROR);
    EXPECT_EQ(vm.getError(), "rewrite rule index out of range");
}

TEST(RewriteLanguageTest, RewriteModeManualRequiresExplicitApply) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["x"], "replacement": ["manual-hit"]}
        rewrite_define ! /
        "manual" rewrite_mode ! /
        "x"
    )");

    auto untouched = vm.popData();
    ASSERT_TRUE(untouched);
    EXPECT_EQ(stringValue(untouched), "x");

    executeScript(vm, R"(
        "x"
        rewrite_apply !
    )");

    auto trace = vm.popData();
    ASSERT_TRUE(trace);
    EXPECT_EQ(stringValue(trace->find("status")->getValue(false, false)), "matched");
    EXPECT_EQ(stringValue(trace->find("mode")->getValue(false, false)), "manual");
    auto rewritten = vm.popData();
    ASSERT_TRUE(rewritten);
    EXPECT_EQ(stringValue(rewritten), "manual-hit");
}

TEST(RewriteLanguageTest, RewriteModeOffDisablesAutomaticRewrites) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["x"], "replacement": ["off-hit"]}
        rewrite_define ! /
        "off" rewrite_mode ! /
        "x"
    )");

    auto result = vm.popData();
    ASSERT_TRUE(result);
    EXPECT_EQ(stringValue(result), "x");

    executeScript(vm, R"(
        rewrite_trace !
    )");
    auto trace = vm.popData();
    ASSERT_TRUE(trace);
    EXPECT_EQ(stringValue(trace->find("status")->getValue(false, false)), "skipped");
    EXPECT_EQ(stringValue(trace->find("reason")->getValue(false, false)), "rewrite mode is off");
}

TEST(RewriteLanguageTest, TypeAwarePatternAndTraceReportMatchDetails) {
    EdictVM vm;
    executeScript(vm, R"(
        {"pattern": ["#atom", "x"], "replacement": ["atom-hit"]}
        rewrite_define ! /
        "tea" "x"
    )");

    auto result = vm.popData();
    ASSERT_TRUE(result);
    EXPECT_EQ(stringValue(result), "atom-hit");

    auto trace = vm.getLastRewriteTrace();
    ASSERT_TRUE(trace);
    EXPECT_EQ(stringValue(trace->find("status")->getValue(false, false)), "matched");
    EXPECT_EQ(stringValue(trace->find("index")->getValue(false, false)), "0");
    EXPECT_EQ(stringValue(listItems(trace->find("pattern")->getValue(false, false))[0]), "#atom");
}
