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

#pragma once

#include "../listree/listree.h"
#include "../core/cursor.h"
#include <deque>
#include <functional>
#include <optional>
#include <vector>
#include <memory>

namespace agentc {
namespace kanren {

CPtr<ListreeValue> createLogicVar(int id);

class State {
public:
    State();
    explicit State(CPtr<ListreeValue> substitution, int varCount);
    CPtr<ListreeValue> walk(CPtr<ListreeValue> v) const;
    std::shared_ptr<State> extend(CPtr<ListreeValue> var, CPtr<ListreeValue> val) const;
    std::shared_ptr<State> unify(CPtr<ListreeValue> u, CPtr<ListreeValue> v) const;
    CPtr<ListreeValue> getSubstitution() const { return sub; }
    int getVarCount() const { return count; }
private:
    CPtr<ListreeValue> sub;
    int count;
};

class StateStream {
public:
    using NextFn = std::function<std::optional<std::shared_ptr<State>>() >;

    StateStream();
    explicit StateStream(std::shared_ptr<State> state);
    explicit StateStream(const std::vector<std::shared_ptr<State>>& states);
    explicit StateStream(NextFn nextFn);

    void push_back(const std::shared_ptr<State>& state);
    std::optional<std::shared_ptr<State>> next();

private:
    std::deque<std::shared_ptr<State>> ready;
    NextFn nextFn;
};

using Goal = std::function<StateStream(std::shared_ptr<State>)>;

// snooze wraps a goal factory in a thunk so the goal is constructed lazily.
// This breaks eager C-stack recursion in self-referential goals (e.g. membero,
// appendo) and converts it to lazy heap recursion via the StateStream pull model.
Goal snooze(std::function<Goal()> thunk);

Goal equal(CPtr<ListreeValue> u, CPtr<ListreeValue> v);
Goal call_fresh(std::function<Goal(CPtr<ListreeValue>)> f);
Goal disj(Goal lhs, Goal rhs);
Goal disj_fair(Goal lhs, Goal rhs);
Goal succeed();
Goal fail();
Goal conj(Goal lhs, Goal rhs);
Goal conj_all(const std::vector<Goal>& goals);
Goal conde(const std::vector<std::vector<Goal>>& clauses);
Goal conde_fair(const std::vector<std::vector<Goal>>& clauses);
Goal conso(CPtr<ListreeValue> head, CPtr<ListreeValue> tail, CPtr<ListreeValue> pair);
Goal heado(CPtr<ListreeValue> pair, CPtr<ListreeValue> head);
Goal tailo(CPtr<ListreeValue> pair, CPtr<ListreeValue> tail);
Goal pairo(CPtr<ListreeValue> pair);
Goal membero(CPtr<ListreeValue> needle, CPtr<ListreeValue> list);
Goal appendo(CPtr<ListreeValue> lhs, CPtr<ListreeValue> rhs, CPtr<ListreeValue> out);

CPtr<ListreeValue> reify(CPtr<ListreeValue> value, std::shared_ptr<State> state);
std::vector<CPtr<ListreeValue>> run(size_t limit, CPtr<ListreeValue> query, Goal goal);
std::vector<CPtr<ListreeValue>> run(size_t limit, const std::vector<CPtr<ListreeValue>>& queries, Goal goal);

} // namespace kanren
} // namespace agentc
