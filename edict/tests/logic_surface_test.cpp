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

#include "../edict_compiler.h"
#include "../edict_vm.h"

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

TEST(LogicSurfaceTest, NativeLogicBlockSupportsMultiVariableResults) {
    auto result = runScript(R"(
        logic {
          "fresh": ["head", "tail"],
          "where": [["conso", "head", "tail", ["tea", "cake"]]],
          "results": ["head", "tail"]
        }
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

TEST(LogicSurfaceTest, NativeLogicBlockFeedsIntoSubsequentEdictWorkflow) {
    auto result = runScript(R"(
        logic {
          "fresh": ["q"],
          "where": [["membero", "q", ["tea", "cake"]]],
          "results": ["q"],
          "limit": "2"
        } @answers
        answers
    )");

    auto values = listToStrings(result);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], "tea");
    EXPECT_EQ(values[1], "cake");
}
