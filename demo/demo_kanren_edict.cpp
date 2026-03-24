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
#include <filesystem>
#include <fstream>
#include <vector>

#include "../cartographer/parser.h"
#include "../cartographer/resolver.h"
#include "../edict/edict_compiler.h"
#include "../edict/edict_vm.h"

using namespace agentc::edict;

int main() {
    EdictVM vm;
    const std::filesystem::path buildDir = std::filesystem::current_path();
    const std::filesystem::path rootDir = buildDir.parent_path();
    const std::filesystem::path libPath = buildDir / "kanren" / "libkanren.so";
    const std::filesystem::path headerPath = rootDir / "cartographer" / "tests" / "kanren_runtime_ffi_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "demo_kanren_runtime_ffi.json";

    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    if (!agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    agentc::cartographer::resolver::ResolvedApi resolved;
    if (!agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    std::ofstream output(resolvedPath);
    if (!output.good()) {
        std::cerr << "failed to write resolved kanren import json\n";
        return 1;
    }
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    output.close();

    std::string script = R"(
        [)" + resolvedPath.string() + R"(] resolver.import_resolved ! @logicffi
        logicffi.agentc_logic_eval_ltv @logic
        {
          "fresh": ["q"],
          "conde": [
            [["==", "q", "tea"]],
            [["membero", "q", ["cake", "jam"]]]
          ],
          "results": ["q"]
        } logic! @answers
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

    std::cout << "Imported Edict logic results:\n";
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
