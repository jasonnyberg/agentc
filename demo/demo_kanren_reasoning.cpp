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

#include <iostream>
#include <algorithm>
#include <string>
#include <vector>

#include "../kanren/kanren.h"

using namespace agentc;
using namespace agentc::kanren;

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

int main() {
    auto memberQuery = createLogicVar(0);
    auto memberAnswers = run(0, memberQuery, membero(memberQuery, makePairList({"tea", "cake", "jam"})));

    std::cout << "membero results:\n";
    for (const auto& answer : memberAnswers) {
        std::cout << "- " << asText(answer) << "\n";
    }

    auto appendQuery = createLogicVar(0);
    auto appendAnswers = run(1, appendQuery, appendo(makePairList({"tea", "cake"}), makePairList({"jam"}), appendQuery));

    std::cout << "appendo result:\n";
    for (const auto& answer : appendAnswers) {
        auto flat = flattenPairList(answer);
        std::cout << "-";
        for (const auto& item : flat) {
            std::cout << ' ' << item;
        }
        std::cout << "\n";
    }

    auto fairQuery = createLogicVar(0);
    auto fairAnswers = run(3, fairQuery, disj_fair(
        conde_fair({
            {equal(fairQuery, createStringValue("left-1"))},
            {equal(fairQuery, createStringValue("left-2"))},
        }),
        equal(fairQuery, createStringValue("right"))
    ));

    std::cout << "fair disjunction results:\n";
    for (const auto& answer : fairAnswers) {
        std::cout << "- " << asText(answer) << "\n";
    }

    auto recursiveFairQuery = createLogicVar(0);
    auto recursiveFairAnswers = run(4, recursiveFairQuery, disj_fair(
        repeatValueFairly(recursiveFairQuery, "left"),
        equal(recursiveFairQuery, createStringValue("right"))
    ));

    std::cout << "fair recursive disjunction results:\n";
    for (const auto& answer : recursiveFairAnswers) {
        std::cout << "- " << asText(answer) << "\n";
    }

    auto head = createLogicVar(0);
    auto tail = createLogicVar(1);
    auto pairAnswers = run(1, {head, tail}, conj_all({
        conso(head, tail, makePairList({"tea", "cake"})),
        pairo(makePairList({"tea", "cake"})),
    }));

    std::cout << "conso/pairo result:\n";
    for (const auto& tuple : pairAnswers) {
        std::vector<CPtr<ListreeValue>> tupleItems;
        tuple->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (ref && ref->getValue()) {
                tupleItems.push_back(ref->getValue());
            }
        });
        std::reverse(tupleItems.begin(), tupleItems.end());
        if (tupleItems.size() == 2) {
            std::cout << "- head=" << asText(tupleItems[0]) << " tail=";
            for (const auto& item : flattenPairList(tupleItems[1])) {
                std::cout << item << ' ';
            }
            std::cout << "\n";
        }
    }

    return 0;
}
