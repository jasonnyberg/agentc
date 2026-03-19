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

#include "../edict/edict_vm.h"

using namespace agentc::edict;

namespace {

BytecodeBuffer pushStrings(const std::vector<std::string>& values) {
    BytecodeBuffer code;
    for (const auto& value : values) {
        code.addOp(VMOP_PUSHEXT);
        code.addValue(Value(value));
    }
    return code;
}

void printStack(const std::vector<std::string>& items) {
    std::cout << '[';
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            std::cout << ", ";
        }
        std::cout << items[i];
    }
    std::cout << "]\n";
}

std::vector<std::string> stackStrings(CPtr<agentc::ListreeValue> items) {
    std::vector<std::string> out;
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

} // namespace

int main() {
    EdictVM vm;
    vm.addRewriteRule({"dup", "dot", "sqrt"}, {"magnitude"});

    auto exact = pushStrings({"dup", "dot", "sqrt"});
    if (vm.execute(exact) & VM_ERROR) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }

    std::cout << "after exact rewrite: ";
    printStack(stackStrings(vm.dumpStack()));

    EdictVM wildcardVm;
    wildcardVm.addRewriteRule({"$1", "x"}, {"x", "$1"});

    auto wildcard = pushStrings({"tea", "x"});
    if (wildcardVm.execute(wildcard) & VM_ERROR) {
        std::cerr << wildcardVm.getError() << "\n";
        return 1;
    }

    std::cout << "after wildcard rewrite: ";
    printStack(stackStrings(wildcardVm.dumpStack()));
    return 0;
}
