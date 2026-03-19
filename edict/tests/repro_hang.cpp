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
#include <filesystem>

using namespace agentc::edict;

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

TEST(ReproHangTest, AssignEvalLoop) {
    EdictVM vm;
    vm.reset();

    // Prepare stack: 
    // ASSIGN needs 2 values (key, value).
    // EVAL needs 1 value (string).
    
    // In the failure case:
    // PUSH 1
    // PUSH "test"
    // ASSIGN (consumes "test" and 1). Stack empty.
    // EVAL (consumes ?). Stack empty.
    
    BytecodeBuffer code;
    code.addOp(VMOP_PUSHEXT); code.addValue(Value("1"));
    code.addOp(VMOP_PUSHEXT); code.addValue(Value("test"));
    code.addOp(VMOP_ASSIGN);
    code.addOp(VMOP_EVAL); // This caused the hang
    
    // Verify it terminates
    vm.execute(code);
    SUCCEED();
}

TEST(ReproFFITest, AddPoC) {
    EdictVM vm;
    vm.reset();

    std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    std::filesystem::path buildDir(TEST_BUILD_DIR);
    
    std::string headerPath = std::filesystem::absolute(sourceDir / "libagentmath_poc.h").string();
    std::string libPath = std::filesystem::absolute(buildDir / "libagentmath_poc.so").string();

    // Edict Code:
    // [libPath] load
    // [headerPath] map @defs
    // 10 32 defs.add !
    
    BytecodeBuffer code;
    // 1. [libPath] load
    code.addOp(VMOP_PUSHEXT); code.addValue(Value(libPath));
    code.addOp(VMOP_LOAD);

    // 2. [headerPath] map @defs
    code.addOp(VMOP_PUSHEXT); code.addValue(Value(headerPath));
    code.addOp(VMOP_MAP);
    code.addOp(VMOP_PUSHEXT); code.addValue(Value("defs"));
    code.addOp(VMOP_ASSIGN);

    // 3. 10 32 defs.add !
    code.addOp(VMOP_PUSHEXT); code.addValue(Value("10"));
    code.addOp(VMOP_PUSHEXT); code.addValue(Value("32"));
    code.addOp(VMOP_PUSHEXT); code.addValue(Value("defs.add"));
    code.addOp(VMOP_REF);
    code.addOp(VMOP_EVAL);

    int state = vm.execute(code);
    ASSERT_FALSE(state & VM_ERROR) << "VM Error: " << vm.getError();

    auto top = vm.getStackTop();
    ASSERT_TRUE(bool(top));
    ASSERT_EQ(top->getLength(), sizeof(int));
    EXPECT_EQ(*(int*)top->getData(), 42);
}