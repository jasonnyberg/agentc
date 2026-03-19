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

#include "../edict_vm.h"
#include "../edict_compiler.h"
#include <iostream>
#include <cassert>
#include <cstring>

using namespace agentc::edict;

namespace {

int decodeIntLikeValue(const CPtr<agentc::ListreeValue>& value) {
    if ((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None && value->getLength() == sizeof(int)) {
        int decoded = 0;
        std::memcpy(&decoded, value->getData(), sizeof(int));
        return decoded;
    }

    if (value->getData() && (value->getFlags() & agentc::LtvFlags::Binary) == agentc::LtvFlags::None) {
        return std::stoi(std::string(static_cast<char*>(value->getData()), value->getLength()));
    }

    throw std::runtime_error("value is not an int-like scalar");
}

} // namespace

void test_splice_postfix() {
    std::cout << "Testing splice postfix ^..." << std::endl;
    
    // Setup
    EdictVM vm(nullptr);
    
    // We want to test that [^x^](1 2 3) pushes 1 2 3 onto listree item x.
    // First we need a listree item x.
    // Let's create a root with x = empty list.
    // But edict starts with empty dict.
    
    // Script:
    // @x []   ; assign empty list to x
    // [^x^](1 2 3) ; should append 1 2 3 to x
    // x       ; push x to verify
    
    std::string source = R"(
        "x" [] @   
        [^x^](1 2 3)
        x
    )";
    
    EdictCompiler compiler;
    BytecodeBuffer code = compiler.compile(source);
    
    vm.execute(code);
    
    auto result = vm.popData();
    assert(result);
    assert(result->isListMode());
    
    // Check contents of x
    int count = 0;
    std::vector<int> values;
    result->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            auto v = ref->getValue();
            values.push_back(decodeIntLikeValue(v));
        }
        count++;
    });
    
    std::cout << "List count: " << count << std::endl;
    for(int v : values) std::cout << " " << v;
    std::cout << std::endl;
    
    assert(count == 3);
    assert(values.size() == 3);
    assert(values[0] == 1);
    assert(values[1] == 2);
    assert(values[2] == 3);
    
    std::cout << "Splice postfix test passed!" << std::endl;
}

int main() {
    try {
        test_splice_postfix();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
