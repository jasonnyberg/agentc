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

#include "logic_evaluator.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "kanren.h"

namespace agentc::kanren {

namespace {

bool valueToString(CPtr<agentc::ListreeValue> value, std::string& out) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return false;
    }
    if ((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        return false;
    }
    out.assign(static_cast<char*>(value->getData()), value->getLength());
    return true;
}

std::vector<CPtr<agentc::ListreeValue>> listValues(CPtr<agentc::ListreeValue> value) {
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

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value, const std::string& name) {
    if (!value) {
        return nullptr;
    }

    if (value->isListMode()) {
        for (const auto& entry : listValues(value)) {
            auto pair = listValues(entry);
            if (pair.size() != 2) {
                continue;
            }
            std::string key;
            if (valueToString(pair[0], key) && key == name) {
                return pair[1];
            }
        }
        return nullptr;
    }

    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

CPtr<agentc::ListreeValue> pairListFromJsonArray(
    CPtr<agentc::ListreeValue> value,
    const std::unordered_map<std::string, CPtr<agentc::ListreeValue>>& vars);

CPtr<agentc::ListreeValue> logicTermFromSpec(
    CPtr<agentc::ListreeValue> value,
    const std::unordered_map<std::string, CPtr<agentc::ListreeValue>>& vars) {
    if (!value) {
        return nullptr;
    }

    if (value->isListMode()) {
        return pairListFromJsonArray(value, vars);
    }

    std::string text;
    if (valueToString(value, text)) {
        auto it = vars.find(text);
        if (it != vars.end()) {
            return it->second;
        }
        return agentc::createStringValue(text);
    }

    auto out = agentc::createNullValue();
    value->forEachTree([&](const std::string& name, CPtr<agentc::ListreeItem>& item) {
        if (!item) {
            return;
        }
        auto child = item->getValue(false, false);
        if (child) {
            agentc::addNamedItem(out, name, logicTermFromSpec(child, vars));
        }
    });
    return out;
}

CPtr<agentc::ListreeValue> pairListFromJsonArray(
    CPtr<agentc::ListreeValue> value,
    const std::unordered_map<std::string, CPtr<agentc::ListreeValue>>& vars) {
    auto items = listValues(value);
    CPtr<agentc::ListreeValue> out = agentc::createNullValue();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        auto pair = agentc::createListValue();
        agentc::addListItem(pair, logicTermFromSpec(*it, vars));
        agentc::addListItem(pair, out);
        out = pair;
    }
    return out;
}

bool appendAtomGoal(
    const std::vector<CPtr<agentc::ListreeValue>>& atom,
    const std::unordered_map<std::string, CPtr<agentc::ListreeValue>>& vars,
    std::vector<agentc::kanren::Goal>& goals,
    std::string& error) {
    if (atom.empty()) {
        error = "Empty logic atom";
        return false;
    }

    std::string op;
    if (!valueToString(atom[0], op)) {
        error = "Logic atom opcode must be a string";
        return false;
    }

    if (op == "==") {
        if (atom.size() != 3) {
            error = "== expects two operands";
            return false;
        }
        goals.push_back(agentc::kanren::equal(
            logicTermFromSpec(atom[1], vars),
            logicTermFromSpec(atom[2], vars)));
        return true;
    }

    if (op == "membero") {
        if (atom.size() != 3) {
            error = "membero expects two operands";
            return false;
        }
        goals.push_back(agentc::kanren::membero(
            logicTermFromSpec(atom[1], vars),
            logicTermFromSpec(atom[2], vars)));
        return true;
    }

    if (op == "appendo") {
        if (atom.size() != 4) {
            error = "appendo expects three operands";
            return false;
        }
        goals.push_back(agentc::kanren::appendo(
            logicTermFromSpec(atom[1], vars),
            logicTermFromSpec(atom[2], vars),
            logicTermFromSpec(atom[3], vars)));
        return true;
    }

    if (op == "conso") {
        if (atom.size() != 4) {
            error = "conso expects three operands";
            return false;
        }
        goals.push_back(agentc::kanren::conso(
            logicTermFromSpec(atom[1], vars),
            logicTermFromSpec(atom[2], vars),
            logicTermFromSpec(atom[3], vars)));
        return true;
    }

    if (op == "heado") {
        if (atom.size() != 3) {
            error = "heado expects two operands";
            return false;
        }
        goals.push_back(agentc::kanren::heado(
            logicTermFromSpec(atom[1], vars),
            logicTermFromSpec(atom[2], vars)));
        return true;
    }

    if (op == "tailo") {
        if (atom.size() != 3) {
            error = "tailo expects two operands";
            return false;
        }
        goals.push_back(agentc::kanren::tailo(
            logicTermFromSpec(atom[1], vars),
            logicTermFromSpec(atom[2], vars)));
        return true;
    }

    if (op == "pairo") {
        if (atom.size() != 2) {
            error = "pairo expects one operand";
            return false;
        }
        goals.push_back(agentc::kanren::pairo(logicTermFromSpec(atom[1], vars)));
        return true;
    }

    error = "Unknown logic atom: " + op;
    return false;
}

bool buildLogicGoal(
    CPtr<agentc::ListreeValue> spec,
    const std::unordered_map<std::string, CPtr<agentc::ListreeValue>>& vars,
    agentc::kanren::Goal& out,
    std::string& error) {
    auto condeValue = namedValue(spec, "conde");
    auto whereValue = namedValue(spec, "where");

    std::vector<std::vector<agentc::kanren::Goal>> clauses;
    auto source = condeValue ? listValues(condeValue) : std::vector<CPtr<agentc::ListreeValue>>{};
    if (!condeValue && whereValue) {
        source.push_back(whereValue);
    }
    if (source.empty()) {
        error = "Logic query requires 'conde' or 'where'";
        if (spec && !spec->isListMode()) {
            std::string keys;
            spec->forEachTree([&](const std::string& name, CPtr<agentc::ListreeItem>&) {
                if (!keys.empty()) {
                    keys += ",";
                }
                keys += name;
            });
            if (!keys.empty()) {
                error += " (available keys: " + keys + ")";
            }
        }
        return false;
    }

    for (const auto& clauseSpec : source) {
        auto atomSpecs = listValues(clauseSpec);
        std::vector<agentc::kanren::Goal> goals;
        for (const auto& atomSpec : atomSpecs) {
            auto atom = listValues(atomSpec);
            if (!appendAtomGoal(atom, vars, goals, error)) {
                return false;
            }
        }
        clauses.push_back(goals);
    }

    out = agentc::kanren::conde(clauses);
    return true;
}

} // namespace

LogicEvalResult evaluateLogicSpec(CPtr<agentc::ListreeValue> spec) {
    LogicEvalResult result;
    if (!spec || spec->isListMode()) {
        result.error = "LOGIC_RUN expects query object";
        return result;
    }

    std::unordered_map<std::string, CPtr<agentc::ListreeValue>> vars;
    auto freshList = listValues(namedValue(spec, "fresh"));
    for (size_t i = 0; i < freshList.size(); ++i) {
        std::string name;
        if (!valueToString(freshList[i], name)) {
            result.error = "fresh entries must be strings";
            return result;
        }
        vars.emplace(name, agentc::kanren::createLogicVar(static_cast<int>(i)));
    }

    agentc::kanren::Goal goal;
    if (!buildLogicGoal(spec, vars, goal, result.error)) {
        return result;
    }

    auto resultTerms = listValues(namedValue(spec, "results"));
    if (resultTerms.empty()) {
        result.error = "Logic query requires 'results'";
        return result;
    }

    size_t limit = 0;
    auto limitValue = namedValue(spec, "limit");
    std::string limitText;
    if (limitValue && valueToString(limitValue, limitText) && !limitText.empty()) {
        limit = static_cast<size_t>(std::strtoul(limitText.c_str(), nullptr, 10));
    }

    auto out = agentc::createListValue();
    if (resultTerms.size() == 1) {
        auto query = logicTermFromSpec(resultTerms[0], vars);
        for (const auto& answer : agentc::kanren::run(limit, query, goal)) {
            agentc::addListItem(out, answer);
        }
    } else {
        std::vector<CPtr<agentc::ListreeValue>> queries;
        for (const auto& term : resultTerms) {
            queries.push_back(logicTermFromSpec(term, vars));
        }
        for (const auto& answer : agentc::kanren::run(limit, queries, goal)) {
            agentc::addListItem(out, answer);
        }
    }

    result.ok = true;
    result.value = out;
    return result;
}

} // namespace agentc::kanren
