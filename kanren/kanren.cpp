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

#include "kanren.h"
#include <algorithm>
#include <cstring>
#include <unordered_map>

namespace agentc {
namespace kanren {

StateStream::StateStream() = default;

StateStream::StateStream(std::shared_ptr<State> state) {
    if (state) {
        ready.push_back(std::move(state));
    }
}

StateStream::StateStream(const std::vector<std::shared_ptr<State>>& states) {
    for (const auto& state : states) {
        if (state) {
            ready.push_back(state);
        }
    }
}

StateStream::StateStream(NextFn fn) : nextFn(std::move(fn)) {}

void StateStream::push_back(const std::shared_ptr<State>& state) {
    if (state) {
        ready.push_back(state);
    }
}

std::optional<std::shared_ptr<State>> StateStream::next() {
    if (!ready.empty()) {
        auto state = ready.front();
        ready.pop_front();
        return state;
    }
    if (!nextFn) {
        return std::nullopt;
    }
    return nextFn();
}

CPtr<ListreeValue> createLogicVar(int id) { return createStringValue(std::to_string(id), LtvFlags::LogicVar); }

namespace {

bool isLogicVar(const CPtr<ListreeValue>& value) {
    return value && (value->getFlags() & LtvFlags::LogicVar) != LtvFlags::None;
}

bool atomicEqual(const CPtr<ListreeValue>& lhs, const CPtr<ListreeValue>& rhs) {
    if (!lhs || !rhs) {
        return !lhs && !rhs;
    }

    if (lhs->isListMode() != rhs->isListMode()) {
        return false;
    }

    if (lhs->getLength() != rhs->getLength()) {
        return false;
    }

    if (lhs->getLength() == 0) {
        return true;
    }

    return std::memcmp(lhs->getData(), rhs->getData(), lhs->getLength()) == 0;
}

std::vector<CPtr<ListreeValue>> listChildren(const CPtr<ListreeValue>& value) {
    std::vector<CPtr<ListreeValue>> out;
    if (!value || !value->isListMode()) {
        return out;
    }

    value->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            out.push_back(ref->getValue());
        }
    }, true);
    return out;
}

std::vector<std::pair<std::string, CPtr<ListreeValue>>> treeChildren(const CPtr<ListreeValue>& value) {
    std::vector<std::pair<std::string, CPtr<ListreeValue>>> out;
    if (!value || value->isListMode()) {
        return out;
    }

    value->forEachTree([&](const std::string& name, CPtr<ListreeItem>& item) {
        if (item) {
            auto child = item->getValue(false, false);
            if (child) {
                out.emplace_back(name, child);
            }
        }
    }, true);
    return out;
}

int maxLogicVarId(const CPtr<ListreeValue>& value) {
    if (!value) {
        return -1;
    }

    if (isLogicVar(value)) {
        return std::stoi(std::string(static_cast<char*>(value->getData()), value->getLength()));
    }

    int maxId = -1;
    if (value->isListMode()) {
        for (auto& child : listChildren(value)) {
            maxId = std::max(maxId, maxLogicVarId(child));
        }
    } else {
        for (const auto& [_, child] : treeChildren(value)) {
            maxId = std::max(maxId, maxLogicVarId(child));
        }
    }

    return maxId;
}

CPtr<ListreeValue> reifyTerm(CPtr<ListreeValue> value,
                             std::shared_ptr<State> state,
                             std::unordered_map<std::string, std::string>& names) {
    if (!state) {
        return value;
    }

    value = state->walk(value);
    if (!value) {
        return nullptr;
    }

    if (isLogicVar(value)) {
        std::string id(static_cast<char*>(value->getData()), value->getLength());
        auto it = names.find(id);
        if (it == names.end()) {
            const std::string label = "_" + std::to_string(names.size());
            it = names.emplace(id, label).first;
        }
        return createStringValue(it->second);
    }

    if (value->isListMode()) {
        auto out = createListValue();
        for (auto& child : listChildren(value)) {
            addListItem(out, reifyTerm(child, state, names));
        }
        return out;
    }

    auto children = treeChildren(value);
    if (!children.empty()) {
        auto out = createNullValue();
        for (const auto& [name, child] : children) {
            addNamedItem(out, name, reifyTerm(child, state, names));
        }
        return out;
    }

    return value;
}

StateStream interleaveStreams(std::vector<std::function<StateStream()>> factories) {
    return StateStream([factories = std::move(factories),
                        streams = std::vector<std::optional<StateStream>>(),
                        index = size_t{0}]() mutable -> std::optional<std::shared_ptr<State>> {
        if (streams.empty()) {
            streams.resize(factories.size());
        }

        if (streams.empty()) {
            return std::nullopt;
        }

        size_t checked = 0;
        while (checked < streams.size()) {
            size_t current = index % streams.size();
            if (!streams[current].has_value()) {
                streams[current].emplace(factories[current]());
            }
            auto next = streams[current]->next();
            ++index;
            ++checked;
            if (next) {
                return next;
            }
        }

        return std::nullopt;
    });
}

} // namespace

State::State() : count(0) { sub = createNullValue(); }
State::State(CPtr<ListreeValue> s, int c) : sub(s), count(c) {}
CPtr<ListreeValue> State::walk(CPtr<ListreeValue> v) const {
    if (!isLogicVar(v)) return v;
    std::string id((char*)v->getData(), v->getLength());
    auto item = sub->find(id);
    if (item && item->getValue()) return walk(item->getValue());
    return v;
}
std::shared_ptr<State> State::extend(CPtr<ListreeValue> var, CPtr<ListreeValue> val) const {
    CPtr<ListreeValue> nSub = sub->copy();
    addNamedItem(nSub, std::string((char*)var->getData(), var->getLength()), val);
    return std::make_shared<State>(nSub, count);
}
std::shared_ptr<State> State::unify(CPtr<ListreeValue> u, CPtr<ListreeValue> v) const {
    u = walk(u); v = walk(v);
    if (u == v) return std::make_shared<State>(sub, count);

    if (isLogicVar(u)) return extend(u, v);
    if (isLogicVar(v)) return extend(v, u);

    if (!u || !v) {
        return nullptr;
    }

    if (u->isListMode() && v->isListMode()) {
        auto lhs = listChildren(u);
        auto rhs = listChildren(v);
        if (lhs.size() != rhs.size()) {
            return nullptr;
        }

        auto current = std::make_shared<State>(sub, count);
        for (size_t i = 0; i < lhs.size(); ++i) {
            current = current->unify(lhs[i], rhs[i]);
            if (!current) {
                return nullptr;
            }
        }
        return current;
    }

    if (!u->isListMode() && !v->isListMode()) {
        auto lhsChildren = treeChildren(u);
        auto rhsChildren = treeChildren(v);
        if (!lhsChildren.empty() || !rhsChildren.empty()) {
            if (lhsChildren.size() != rhsChildren.size()) {
                return nullptr;
            }

            auto current = std::make_shared<State>(sub, count);
            for (size_t i = 0; i < lhsChildren.size(); ++i) {
                if (lhsChildren[i].first != rhsChildren[i].first) {
                    return nullptr;
                }
                current = current->unify(lhsChildren[i].second, rhsChildren[i].second);
                if (!current) {
                    return nullptr;
                }
            }
            return current;
        }
    }

    if (atomicEqual(u, v)) return std::make_shared<State>(sub, count);
    return nullptr;
}

// snooze wraps a goal factory in a thunk so the goal is not constructed until
// the stream is first pulled. This breaks eager C-stack recursion in recursive
// goals (membero, appendo) by turning each recursive self-call into a lazy
// heap-allocated closure instead of an immediate C-frame.
Goal snooze(std::function<Goal()> thunk) {
    return [thunk = std::move(thunk)](std::shared_ptr<State> s) -> StateStream {
        return StateStream([thunk, s, started = false,
                            inner = std::optional<StateStream>{}]() mutable
                           -> std::optional<std::shared_ptr<State>> {
            if (!started) {
                started = true;
                inner.emplace(thunk()(s));
            }
            return inner->next();
        });
    };
}

Goal equal(CPtr<ListreeValue> u, CPtr<ListreeValue> v) {
    return [u, v](std::shared_ptr<State> s) -> StateStream {
        auto res = s->unify(u, v);
        return res ? StateStream(res) : StateStream();
    };
}
Goal call_fresh(std::function<Goal(CPtr<ListreeValue>)> f) {
    return [f](std::shared_ptr<State> s) -> StateStream {
        auto var = createLogicVar(s->getVarCount());
        return f(var)(std::make_shared<State>(s->getSubstitution(), s->getVarCount() + 1));
    };
}

Goal disj(Goal lhs, Goal rhs) {
    return [lhs, rhs](std::shared_ptr<State> s) -> StateStream {
        auto left = lhs(s);
        auto right = rhs(s);
        return StateStream([left = std::move(left), right = std::move(right), usingLeft = true]() mutable -> std::optional<std::shared_ptr<State>> {
            if (usingLeft) {
                auto next = left.next();
                if (next) {
                    return next;
                }
                usingLeft = false;
            }
            return right.next();
        });
    };
}

Goal disj_fair(Goal lhs, Goal rhs) {
    return [lhs, rhs](std::shared_ptr<State> s) -> StateStream {
        return interleaveStreams({
            [lhs, s]() { return lhs(s); },
            [rhs, s]() { return rhs(s); },
        });
    };
}

Goal succeed() {
    return [](std::shared_ptr<State> s) -> StateStream {
        return StateStream(s);
    };
}

Goal fail() {
    return [](std::shared_ptr<State>) -> StateStream {
        return StateStream();
    };
}

Goal conj(Goal lhs, Goal rhs) {
    return [lhs, rhs](std::shared_ptr<State> s) -> StateStream {
        auto left = lhs(s);
        return StateStream([left = std::move(left), rhs, current = std::optional<StateStream>{}]() mutable -> std::optional<std::shared_ptr<State>> {
            while (true) {
                if (current) {
                    auto next = current->next();
                    if (next) {
                        return next;
                    }
                    current.reset();
                }

                auto leftState = left.next();
                if (!leftState) {
                    return std::nullopt;
                }
                current.emplace(rhs(*leftState));
            }
        });
    };
}

Goal conj_all(const std::vector<Goal>& goals) {
    if (goals.empty()) {
        return succeed();
    }

    Goal combined = goals.front();
    for (size_t i = 1; i < goals.size(); ++i) {
        combined = conj(combined, goals[i]);
    }
    return combined;
}

Goal conde(const std::vector<std::vector<Goal>>& clauses) {
    return [clauses](std::shared_ptr<State> s) -> StateStream {
        std::vector<std::function<StateStream()>> factories;
        factories.reserve(clauses.size());
        for (const auto& clause : clauses) {
            factories.push_back([clause, s]() {
                return conj_all(clause)(s);
            });
        }
        return StateStream([factories = std::move(factories),
                            streams = std::vector<std::optional<StateStream>>(),
                            index = size_t{0}]() mutable -> std::optional<std::shared_ptr<State>> {
            if (streams.empty()) {
                streams.resize(factories.size());
            }
            while (index < streams.size()) {
                if (!streams[index].has_value()) {
                    streams[index].emplace(factories[index]());
                }
                auto next = streams[index]->next();
                if (next) {
                    return next;
                }
                ++index;
            }
            return std::nullopt;
        });
    };
}

Goal conde_fair(const std::vector<std::vector<Goal>>& clauses) {
    return [clauses](std::shared_ptr<State> s) -> StateStream {
        std::vector<std::function<StateStream()>> factories;
        factories.reserve(clauses.size());
        for (const auto& clause : clauses) {
            factories.push_back([clause, s]() {
                return conj_all(clause)(s);
            });
        }
        return interleaveStreams(std::move(factories));
    };
}

Goal conso(CPtr<ListreeValue> head, CPtr<ListreeValue> tail, CPtr<ListreeValue> pair) {
    auto out = createListValue();
    addListItem(out, head);
    addListItem(out, tail);
    return equal(out, pair);
}

Goal heado(CPtr<ListreeValue> pair, CPtr<ListreeValue> head) {
    return call_fresh([=](CPtr<ListreeValue> tail) {
        return conso(head, tail, pair);
    });
}

Goal tailo(CPtr<ListreeValue> pair, CPtr<ListreeValue> tail) {
    return call_fresh([=](CPtr<ListreeValue> head) {
        return conso(head, tail, pair);
    });
}

Goal pairo(CPtr<ListreeValue> pair) {
    return call_fresh([=](CPtr<ListreeValue> head) {
        return call_fresh([=](CPtr<ListreeValue> tail) {
            return conso(head, tail, pair);
        });
    });
}

// membero(needle, list): needle is a member of the pair-list `list`.
// The recursive self-call is wrapped in snooze() so that each step is
// constructed lazily (one C frame, not O(N) frames eagerly). This prevents
// C-stack overflow on long lists and aligns with mini-Kanren's stream model.
Goal membero(CPtr<ListreeValue> needle, CPtr<ListreeValue> list) {
    return call_fresh([=](CPtr<ListreeValue> head) {
        return call_fresh([=](CPtr<ListreeValue> tail) {
            return conj(
                conso(head, tail, list),
                conde_fair({
                    {equal(needle, head)},
                    {snooze([=]() { return membero(needle, tail); })},
                })
            );
        });
    });
}

// appendo(lhs, rhs, out): out = lhs ++ rhs.
// The recursive self-call is wrapped in snooze() for the same reason as
// membero: prevents eager O(N) C-stack growth on long lists.
Goal appendo(CPtr<ListreeValue> lhs, CPtr<ListreeValue> rhs, CPtr<ListreeValue> out) {
    return conde_fair({
        {
            equal(lhs, createNullValue()),
            equal(rhs, out),
        },
        {
            call_fresh([=](CPtr<ListreeValue> head) {
                return call_fresh([=](CPtr<ListreeValue> tail) {
                    return call_fresh([=](CPtr<ListreeValue> rest) {
                        return conj_all({
                            conso(head, tail, lhs),
                            conso(head, rest, out),
                            snooze([=]() { return appendo(tail, rhs, rest); }),
                        });
                    });
                });
            }),
        },
    });
}

CPtr<ListreeValue> reify(CPtr<ListreeValue> value, std::shared_ptr<State> state) {
    std::unordered_map<std::string, std::string> names;
    return reifyTerm(value, state, names);
}

std::vector<CPtr<ListreeValue>> run(size_t limit, CPtr<ListreeValue> query, Goal goal) {
    auto initial = std::make_shared<State>(createNullValue(), maxLogicVarId(query) + 1);
    std::vector<CPtr<ListreeValue>> out;
    auto stream = goal(initial);
    while (auto state = stream.next()) {
        out.push_back(reify(query, *state));
        if (limit > 0 && out.size() >= limit) {
            break;
        }
    }
    return out;
}

std::vector<CPtr<ListreeValue>> run(size_t limit, const std::vector<CPtr<ListreeValue>>& queries, Goal goal) {
    int maxId = -1;
    for (const auto& query : queries) {
        maxId = std::max(maxId, maxLogicVarId(query));
    }

    auto initial = std::make_shared<State>(createNullValue(), maxId + 1);
    std::vector<CPtr<ListreeValue>> out;
    auto stream = goal(initial);
    while (auto state = stream.next()) {
        auto tuple = createListValue();
        for (const auto& query : queries) {
            addListItem(tuple, reify(query, *state));
        }
        out.push_back(tuple);
        if (limit > 0 && out.size() >= limit) {
            break;
        }
    }
    return out;
}

} // namespace kanren
} // namespace agentc
