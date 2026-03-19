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

#include <gtest/gtest.h>
#include "../edict_vm.h"
#include "../edict_types.h"
#include "../edict_types.h"
#include "../edict_compiler.h"

using namespace agentc::edict;

static std::string valueToString(const CPtr<agentc::ListreeValue>& v) {
    if (!v || !v->getData() || v->getLength() == 0) return "";
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return "";
    return std::string(static_cast<char*>(v->getData()), v->getLength());
}

static int runCode(EdictVM& vm, const std::string& source) {
    EdictCompiler compiler;
    BytecodeBuffer code = compiler.compile(source);
    return vm.execute(code);
}

TEST(EdictVM, EvalTest) {
    EdictVM vm;
    BytecodeBuffer code;
    code.addOp(VMOP_RESET);
    code.addOp(VMOP_PUSHEXT);
    code.addValue(Value("1 2"));
    code.addOp(VMOP_EVAL);
    int res = vm.execute(code);
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getStackSize(), 2);
}

TEST(EdictVM, AutoDerefAssignment) {
    EdictVM vm;
    int res = runCode(vm, "[hello]@x x");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "hello");
}

TEST(EdictVM, UndefinedSymbolFallsBack) {
    EdictVM vm;
    int res = runCode(vm, "not_defined");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "not_defined");
}

TEST(EdictVM, RemoveSymbol) {
    EdictVM vm;
    int res = runCode(vm, "[hello]@x /x x");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "x");
}

TEST(EdictVM, RemoveTopOfStack) {
    EdictVM vm;
    int res = runCode(vm, "1 2 3 /");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getStackSize(), 2);
    auto top = vm.getStackTop();
    EXPECT_TRUE(bool(top));
}

TEST(EdictVM, DeferredEvalWithMethodCall) {
    EdictVM vm;
    int res = runCode(vm, "[!]@myeval [dup]@twice myeval([hello] twice)");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "hello");

    BytecodeBuffer popCode;
    popCode.addOp(VMOP_POP);
    res = vm.execute(popCode);
    EXPECT_FALSE(res & 0x02);
    top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "hello");
}

TEST(EdictVM, NestedEvalUsesCodeStack) {
    EdictVM vm;
    int res = runCode(vm, "[[hello] !] !");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "hello");
}
