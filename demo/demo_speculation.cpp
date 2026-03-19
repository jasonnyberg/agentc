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
#include "../edict/edict_compiler.h"
#include "../edict/edict_vm.h"

using namespace agentc::edict;

namespace {

int executeSingle(EdictVM& vm, VMOpcode op) {
    BytecodeBuffer code;
    code.addOp(op);
    return vm.execute(code);
}

} // namespace

int main() {
    EdictVM vm;

    std::cout << "baseline stack depth: " << vm.getResourceDepth(VMRES_STACK) << "\n";
    std::cout << "baseline dict depth:  " << vm.getResourceDepth(VMRES_DICT) << "\n";

    auto checkpoint = vm.beginTransaction();
    if (!checkpoint.valid) {
        std::cerr << "failed to start transaction\n";
        return 1;
    }

    vm.pushData(agentc::createNullValue());
    if (executeSingle(vm, VMOP_CTX_PUSH) & VM_ERROR) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }

    vm.pushData(agentc::createStringValue("temp"));
    std::cout << "during stack depth:       " << vm.getResourceDepth(VMRES_STACK) << "\n";
    std::cout << "during dict depth:        " << vm.getResourceDepth(VMRES_DICT) << "\n";
    std::cout << "during stack size:        " << vm.getStackSize() << "\n";

    if (!vm.rollbackTransaction(checkpoint)) {
        std::cerr << "failed to rollback transaction\n";
        return 1;
    }
    std::cout << "final stack depth:        " << vm.getResourceDepth(VMRES_STACK) << "\n";
    std::cout << "final dict depth:         " << vm.getResourceDepth(VMRES_DICT) << "\n";
    std::cout << "final stack size:         " << vm.getStackSize() << "\n";

    CPtr<agentc::ListreeValue> result;
    std::string error;
    if (!vm.speculateValue([](EdictVM& speculativeVm) {
            auto trial = agentc::createStringValue("trial-result");
            speculativeVm.pushData(trial);
            return trial;
        }, result, error)) {
        std::cerr << "speculation failed: " << error << "\n";
        return 1;
    }

    std::cout << "speculative result copy:  ";
    if (result && result->getData()) {
        std::cout << std::string(static_cast<char*>(result->getData()), result->getLength()) << "\n";
    } else {
        std::cout << "<null>\n";
    }
    std::cout << "post-spec stack size:     " << vm.getStackSize() << "\n";

    BytecodeBuffer syntaxCode = EdictCompiler().compile("\"baseline\" speculate [\"syntax-result\"]");
    if (vm.execute(syntaxCode) & VM_ERROR) {
        std::cerr << "syntax speculation failed: " << vm.getError() << "\n";
        return 1;
    }
    auto syntaxResult = vm.popData();
    auto syntaxBaseline = vm.popData();
    std::cout << "syntax speculate result:  "
              << (syntaxResult && syntaxResult->getData()
                      ? std::string(static_cast<char*>(syntaxResult->getData()), syntaxResult->getLength())
                      : std::string("<null>"))
              << "\n";
    std::cout << "syntax baseline remains:  "
              << (syntaxBaseline && syntaxBaseline->getData()
                      ? std::string(static_cast<char*>(syntaxBaseline->getData()), syntaxBaseline->getLength())
                      : std::string("<null>"))
              << "\n";

    std::cout << "note: current speculation helper restores state automatically; full O(1) watermark rollback remains future work\n";
    return 0;
}
