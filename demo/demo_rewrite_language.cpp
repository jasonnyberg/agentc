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
#include <vector>

#include "../edict/edict_compiler.h"
#include "../edict/edict_vm.h"

using namespace agentc::edict;

namespace {

std::vector<std::string> stackStrings(EdictVM& vm) {
    std::vector<std::string> out;
    auto items = vm.dumpStack();
    if (!items) return out;
    items->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (!ref || !ref->getValue() || !ref->getValue()->getData()) {
            out.emplace_back();
            return;
        }
        auto value = ref->getValue();
        out.emplace_back(static_cast<char*>(value->getData()), value->getLength());
    }, false);
    return out;
}

void printItems(const std::vector<std::string>& items) {
    std::cout << '[';
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            std::cout << ", ";
        }
        std::cout << items[i];
    }
    std::cout << "]\n";
}

std::string traceField(CPtr<agentc::ListreeValue> trace, const std::string& name) {
    if (!trace) {
        return {};
    }
    auto item = trace->find(name);
    if (!item || !item->getValue(false, false) || !item->getValue(false, false)->getData()) {
        return {};
    }
    auto value = item->getValue(false, false);
    return std::string(static_cast<char*>(value->getData()), value->getLength());
}

} // namespace

int main() {
    EdictVM vm;

    std::string script = R"(
        {"pattern": ["dup", "dot", "sqrt"], "replacement": ["magnitude"]}
        rewrite_define ! /
        {"pattern": ["$1", "x"], "replacement": ["x", "$1"]}
        rewrite_define ! /
        "dup" "dot" "sqrt"
    )";

    auto code = EdictCompiler().compile(script);
    if (vm.execute(code) & VM_ERROR) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }

    std::cout << "after source-defined optimizer rule: ";
    printItems(stackStrings(vm));

    EdictVM wildcardVm;
    std::string wildcardScript = R"(
        {"pattern": ["$1", "x"], "replacement": ["x", "$1"]}
        rewrite_define ! /
        "tea" "x"
    )";

    auto wildcardCode = EdictCompiler().compile(wildcardScript);
    if (wildcardVm.execute(wildcardCode) & VM_ERROR) {
        std::cerr << wildcardVm.getError() << "\n";
        return 1;
    }

    std::cout << "after source-defined wildcard rule: ";
    printItems(stackStrings(wildcardVm));

    EdictVM scopedVm;
    std::string scopedScript = R"(
        {"pattern": ["x"], "replacement": ["manual-hit"]}
        rewrite_define ! /
        "manual" rewrite_mode ! /
        "x"
    )";

    auto scopedCode = EdictCompiler().compile(scopedScript);
    if (scopedVm.execute(scopedCode) & VM_ERROR) {
        std::cerr << scopedVm.getError() << "\n";
        return 1;
    }

    std::cout << "manual rewrite mode leaves stack as: ";
    printItems(stackStrings(scopedVm));

    auto applyCode = EdictCompiler().compile(R"(
        rewrite_apply !
    )");
    if (scopedVm.execute(applyCode) & VM_ERROR) {
        std::cerr << scopedVm.getError() << "\n";
        return 1;
    }

    std::cout << "after manual rewrite apply: ";
    printItems(stackStrings(scopedVm));
    auto trace = scopedVm.getLastRewriteTrace();
    std::cout << "last rewrite trace: status=" << traceField(trace, "status")
              << " mode=" << traceField(trace, "mode")
              << " reason=" << traceField(trace, "reason") << "\n";

    EdictVM introspectionVm;
    std::string introspectionScript = R"(
        {"pattern": ["alpha"], "replacement": ["first"]}
        rewrite_define ! /
        {"pattern": ["beta"], "replacement": ["second"]}
        rewrite_define ! /
        "0" rewrite_remove ! /
        rewrite_list !
    )";

    auto introspectionCode = EdictCompiler().compile(introspectionScript);
    if (introspectionVm.execute(introspectionCode) & VM_ERROR) {
        std::cerr << introspectionVm.getError() << "\n";
        return 1;
    }

    std::cout << "active rewrite rules after removal: " << introspectionVm.getRewriteRuleCount() << "\n";
    return 0;
}
