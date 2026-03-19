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
#include <vector>

#include "../edict/edict_compiler.h"
#include "../edict/edict_vm.h"

using namespace agentc::edict;

int main() {
    EdictVM vm;
    std::string script = R"(
        logic {
          "fresh": ["q"],
          "conde": [
            [["==", "q", "tea"]],
            [["membero", "q", ["cake", "jam"]]]
          ],
          "results": ["q"]
        } @answers
        answers
    )";

    auto code = EdictCompiler().compile(script);
    if (vm.execute(code) & VM_ERROR) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }

    auto result = vm.popData();
    if (!result || !result->isListMode()) {
        std::cerr << "logic returned unexpected value\n";
        return 1;
    }

    std::cout << "Edict logic block results:\n";
    std::vector<std::string> values;
    result->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (!ref || !ref->getValue() || !ref->getValue()->getData()) {
            return;
        }
        values.emplace_back(static_cast<char*>(ref->getValue()->getData()), ref->getValue()->getLength());
    });
    std::reverse(values.begin(), values.end());
    for (const auto& value : values) {
        std::cout << "- " << value << "\n";
    }
    return 0;
}
