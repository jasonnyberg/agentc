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
#include "../kanren.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace agentc::kanren;
using namespace agentc;

namespace {

std::string asText(CPtr<ListreeValue> value) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return "";
    }

    return std::string(static_cast<char*>(value->getData()), value->getLength());
}

CPtr<ListreeValue> pairValue(CPtr<ListreeValue> head, CPtr<ListreeValue> tail) {
    auto pair = createListValue();
    addListItem(pair, head);
    addListItem(pair, tail);
    return pair;
}

CPtr<ListreeValue> makePairList(const std::vector<std::string>& values) {
    CPtr<ListreeValue> out = createNullValue();
    for (auto it = values.rbegin(); it != values.rend(); ++it) {
        out = pairValue(createStringValue(*it), out);
    }
    return out;
}

std::vector<std::string> flattenPairList(CPtr<ListreeValue> value) {
    std::vector<std::string> out;
    auto current = value;

    while (current && current->isListMode()) {
        std::vector<CPtr<ListreeValue>> items;
        current->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (ref && ref->getValue()) {
                items.push_back(ref->getValue());
            }
        });

        if (items.size() != 2) {
            break;
        }

        out.push_back(asText(items[0]));
        current = items[1];
    }

    return out;
}

Goal repeatValueFairly(CPtr<ListreeValue> query, const std::string& value) {
    return [query, value](std::shared_ptr<State> s) -> StateStream {
        return disj_fair(
            equal(query, createStringValue(value)),
            repeatValueFairly(query, value)
        )(s);
    };
}

} // namespace

TEST(KanrenTest, Unification) {
    State s;
    auto varX = createLogicVar(0);
    auto val1 = createStringValue("1");
    auto s1 = s.unify(varX, val1);
    ASSERT_TRUE(s1 != nullptr);
    auto walked = s1->walk(varX);
    EXPECT_EQ(asText(walked), "1");
}

TEST(KanrenTest, DisjunctionProducesMultipleResults) {
    auto query = createLogicVar(0);
    auto answers = run(0, query, conde({
        {equal(query, createStringValue("tea"))},
        {equal(query, createStringValue("coffee"))},
    }));

    ASSERT_EQ(answers.size(), 2u);
    EXPECT_EQ(asText(answers[0]), "tea");
    EXPECT_EQ(asText(answers[1]), "coffee");
}

TEST(KanrenTest, ConjunctionThreadsSubstitutions) {
    auto query = createLogicVar(0);
    auto answers = run(0, query, conj_all({
        equal(query, createStringValue("tea")),
        equal(query, createStringValue("tea")),
    }));

    ASSERT_EQ(answers.size(), 1u);
    EXPECT_EQ(asText(answers[0]), "tea");
}

TEST(KanrenTest, ReifyNamesUnboundVariables) {
    auto x = createLogicVar(0);
    auto y = createLogicVar(1);
    auto answers = run(1, x, equal(x, y));

    ASSERT_EQ(answers.size(), 1u);
    EXPECT_EQ(asText(answers[0]), "_0");
}

TEST(KanrenTest, MemberoFindsMatchesRecursively) {
    auto query = createLogicVar(0);
    auto list = makePairList({"tea", "cake", "tea"});
    auto answers = run(0, query, membero(query, list));

    ASSERT_EQ(answers.size(), 3u);
    EXPECT_EQ(asText(answers[0]), "tea");
    EXPECT_EQ(asText(answers[1]), "cake");
    EXPECT_EQ(asText(answers[2]), "tea");
}

TEST(KanrenTest, AppendoBuildsOutputRecursively) {
    auto out = createLogicVar(0);
    auto lhs = makePairList({"tea", "cake"});
    auto rhs = makePairList({"jam"});
    auto answers = run(1, out, appendo(lhs, rhs, out));

    ASSERT_EQ(answers.size(), 1u);
    auto flat = flattenPairList(answers[0]);
    ASSERT_EQ(flat.size(), 3u);
    EXPECT_EQ(flat[0], "tea");
    EXPECT_EQ(flat[1], "cake");
    EXPECT_EQ(flat[2], "jam");
}

TEST(KanrenTest, FairDisjunctionInterleavesFiniteBranches) {
    auto query = createLogicVar(0);
    auto left = conde_fair({
        {equal(query, createStringValue("left-1"))},
        {equal(query, createStringValue("left-2"))},
    });
    auto answers = run(3, query, disj_fair(
        left,
        equal(query, createStringValue("right"))
    ));

    ASSERT_EQ(answers.size(), 3u);
    EXPECT_EQ(asText(answers[0]), "left-1");
    EXPECT_EQ(asText(answers[1]), "right");
    EXPECT_EQ(asText(answers[2]), "left-2");
}

TEST(KanrenTest, FairDisjunctionReachesLaterBranchDespiteRecursiveProducer) {
    auto query = createLogicVar(0);
    auto answers = run(4, query, disj_fair(
        repeatValueFairly(query, "left"),
        equal(query, createStringValue("right"))
    ));

    ASSERT_EQ(answers.size(), 4u);
    EXPECT_EQ(asText(answers[0]), "left");
    EXPECT_EQ(asText(answers[1]), "right");
    EXPECT_EQ(asText(answers[2]), "left");
    EXPECT_EQ(asText(answers[3]), "left");
}

TEST(KanrenTest, ConsoHeadoAndTailoRelatePairStructure) {
    auto head = createLogicVar(0);
    auto tail = createLogicVar(1);
    auto tupleAnswers = run(1, {head, tail}, conj_all({
        conso(head, tail, makePairList({"tea", "cake"})),
        heado(makePairList({"tea", "cake"}), head),
        tailo(makePairList({"tea", "cake"}), tail),
    }));

    ASSERT_EQ(tupleAnswers.size(), 1u);
    std::vector<CPtr<ListreeValue>> tupleItems;
    tupleAnswers[0]->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            tupleItems.push_back(ref->getValue());
        }
    });
    std::reverse(tupleItems.begin(), tupleItems.end());
    ASSERT_EQ(tupleItems.size(), 2u);
    EXPECT_EQ(asText(tupleItems[0]), "tea");
    auto tailFlat = flattenPairList(tupleItems[1]);
    ASSERT_EQ(tailFlat.size(), 1u);
    EXPECT_EQ(tailFlat[0], "cake");
}

TEST(KanrenTest, PairoRejectsNullAndMultiQueryEmptyCaseStaysEmpty) {
    auto x = createLogicVar(0);
    auto y = createLogicVar(1);
    auto answers = run(2, {x, y}, conj_all({
        pairo(createNullValue()),
        equal(x, createStringValue("tea")),
        equal(y, createStringValue("cake")),
    }));

    EXPECT_TRUE(answers.empty());
}

// G010.1: membero on a large list must not overflow the C stack.
// Without snooze(), each recursive call to membero() at goal-construction time
// occupies one C frame; many elements would overflow. With snooze() each
// recursive call is deferred to the stream pull loop (heap recursion, not
// C-stack), so goal construction is O(1) C frames regardless of list length.
//
// N=500 is well above the ~200-frame depth where C-stack overflow would occur
// without snooze (each membero/call_fresh frame pair uses ~several-hundred bytes),
// while run(3, ...) pulls only 3 answers so enumeration cost stays small.
TEST(KanrenTest, MemberoLargeListNoStackOverflow) {
    const int N = 500;
    std::vector<std::string> elems;
    elems.reserve(N);
    for (int i = 0; i < N; ++i) {
        elems.push_back(std::to_string(i));
    }
    auto list = makePairList(elems);

    auto query = createLogicVar(0);
    // Pull only the first 3 answers — proving construction doesn't blow the stack.
    auto answers = run(3, query, membero(query, list));
    ASSERT_EQ(answers.size(), 3u);
    EXPECT_EQ(asText(answers[0]), "0");
    EXPECT_EQ(asText(answers[1]), "1");
    EXPECT_EQ(asText(answers[2]), "2");
}

// G010.2: conde_fair interleaves; a second branch is reachable even when the
// first branch produces an infinite stream. This verifies fairness.
TEST(KanrenTest, CondeFairReachesSecondBranchDespiteInfiniteFirst) {
    auto query = createLogicVar(0);
    // Branch 1: infinite stream of "inf" via disj_fair with repeatValueFairly
    // Branch 2: finite answer "finite"
    auto answers = run(4, query, conde_fair({
        {repeatValueFairly(query, "inf")},
        {equal(query, createStringValue("finite"))},
    }));

    ASSERT_EQ(answers.size(), 4u);
    // "finite" must appear somewhere in the results (fairness guarantee).
    bool foundFinite = false;
    for (const auto& a : answers) {
        if (asText(a) == "finite") {
            foundFinite = true;
            break;
        }
    }
    EXPECT_TRUE(foundFinite) << "conde_fair must interleave: 'finite' should be reachable";
}

// G010.2: Nested conde_fair — verifies that interleaving composes correctly
// across multiple levels.
TEST(KanrenTest, NestedCondeFairProducesAllAnswers) {
    auto q = createLogicVar(0);
    auto answers = run(0, q, conde_fair({
        {conde_fair({
            {equal(q, createStringValue("a"))},
            {equal(q, createStringValue("b"))},
        })},
        {conde_fair({
            {equal(q, createStringValue("c"))},
            {equal(q, createStringValue("d"))},
        })},
    }));

    ASSERT_EQ(answers.size(), 4u);
    std::vector<std::string> got;
    got.reserve(answers.size());
    for (const auto& a : answers) {
        got.push_back(asText(a));
    }
    std::sort(got.begin(), got.end());
    EXPECT_EQ(got, (std::vector<std::string>{"a", "b", "c", "d"}));
}
