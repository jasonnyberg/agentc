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
#include <fstream>
#include "../listree/listree.h"
#include "../core/cursor.h"
#include "../cartographer/mapper.h"
#include "../cartographer/ffi.h"
#include "../kanren/kanren.h"
#include "../edict/edict_vm.h"
#include "../edict/edict_compiler.h"

using namespace agentc;
using namespace agentc::cartographer;
using namespace agentc::kanren;
using namespace agentc::edict;

void demo_header_mapping_and_ffi() {
    std::cout << "\n=== 1. Header Mapping & FFI Demo ===" << std::endl;
    std::string headerPath = "demo_math.h";
    { std::ofstream ofs(headerPath); ofs << "int abs(int n);" << std::endl; ofs << "struct Point { int x; int y; };" << std::endl; } // Corrected: Removed unnecessary escaping of newlines
    Mapper mapper;
    CPtr<ListreeValue> root = mapper.parse(headerPath);
    Cursor cursor(root);
    if (cursor.resolve("Point"))
        std::cout << "Found Struct: " << cursor.getName() << std::endl;
    FFI ffi;
    if (ffi.loadLibrary("libc.so.6") || ffi.loadLibrary("/lib/x86_64-linux-gnu/libc.so.6")) {
        auto absFunc = root->find("abs");
        if (absFunc && absFunc->getValue()) {
            int input = -100;
            auto args = createListValue();
            addListItem(args, createBinaryValue(&input, sizeof(int)));
            auto result = ffi.invoke("abs", absFunc->getValue(), args);
            if (result)
                std::cout << "abs(-100) = " << *(int*)result->getData() << std::endl;
        }
    }
}

void demo_kanren() {
    std::cout << "\n=== 2. Mini-Kanren Logic Demo ===" << std::endl;
    auto val5 = createStringValue("5");
    auto goal = call_fresh([&](CPtr<ListreeValue> q) { return equal(q, val5); });
    auto stream = goal(std::make_shared<State>());
    auto firstState = stream.next();
    if (firstState.has_value()) {
        auto ans = (*firstState)->walk(createLogicVar(0));
        std::cout << "Solved: q == " << std::string((char*)ans->getData(), ans->getLength()) << std::endl;
    }
}

void demo_traversal() {
    std::cout << "\n=== 3. Traversal & Cycle Detection Demo ===" << std::endl;
    auto a = createStringValue("A");
    auto b = createStringValue("B");
    addNamedItem(a, "next", b);
    addNamedItem(b, "next", a);
    std::cout << "Traversing cyclic graph..." << std::endl;
    a->traverse([](CPtr<ListreeValue> val) {
        if (val && val->getData())
            std::cout << "Visited: " << std::string((char*)val->getData(), val->getLength()) << std::endl;
    });
}

void demo_edict() {
    std::cout << "\n=== 4. Edict VM Demo ===" << std::endl;
    
    // Create root dictionary and register builtins
    auto root = createNullValue();
    addNamedItem(root, "print", createStringValue("print"));
    
    EdictVM vm(root);
    EdictCompiler compiler;
    
    std::string source = "print([hello]) [world] print";
    std::cout << "Executing Edict script: " << source << std::endl;
    try {
        BytecodeBuffer code = compiler.compile(source);
        vm.execute(code);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

int main() {
    demo_header_mapping_and_ffi();
    demo_kanren();
    demo_traversal();
    demo_edict();
    return 0;
}
