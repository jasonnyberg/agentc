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
#include "../edict_compiler.h"
#include <iostream>

using namespace agentc::edict;

static std::string valueToString(const CPtr<agentc::ListreeValue>& v) {
    if (!v || !v->getData() || v->getLength() == 0) return "";
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return "";
    return std::string(static_cast<char*>(v->getData()), v->getLength());
}

class VMStackTest : public ::testing::Test {
protected:
    EdictVM vm;
    EdictCompiler compiler;

    void SetUp() override {
        vm.reset();
    }

    void execute(const std::string& code) {
        BytecodeBuffer bc = compiler.compile(code);
        vm.execute(bc);
    }
};

// Test 1: If-Then (Condition True)
TEST_F(VMStackTest, IfThenTrue) {
    // [1] test & [true] @res | [false] @res
    execute("[1] test & [true] @res | [false] @res");
    execute("res");
    EXPECT_EQ(valueToString(vm.getStackTop()), "true");
}

// Test 2: If-Then (Condition False)
TEST_F(VMStackTest, IfThenFalse) {
    execute("[] test & [true] @res | [false] @res");
    execute("res");
    EXPECT_EQ(valueToString(vm.getStackTop()), "false");
}

// Test 3: If Only (No Else)
TEST_F(VMStackTest, IfOnly) {
    execute("[1] test & [true] @res");
    execute("res");
    EXPECT_EQ(valueToString(vm.getStackTop()), "true");
    
    execute("[] test & [false] @res2");
    // res2 should not be set (undefined -> pushes "res2")
    execute("res2");
    EXPECT_EQ(valueToString(vm.getStackTop()), "res2");
}

// Test 4: Nested If
TEST_F(VMStackTest, NestedIf) {
    execute("[1] test & [1] test & [inner] @res | [skip] @res | [skip] @res");
    execute("res");
    EXPECT_EQ(valueToString(vm.getStackTop()), "inner");
}

// Test 5: Explicit Fail
TEST_F(VMStackTest, ExplicitFail) {
    // [oops] fail & [recovered] @res | [failed] @res
    // 'fail' pushes [oops] to STATE.
    // '&' sees STATE not empty.
    // Pops [oops], pushes to DATA.
    // Sets SCANNING.
    // Skips [recovered] @res.
    // Hits |. Resumes.
    // Executes [failed] @res.
    
    execute("[oops] fail & [recovered] @res | [failed] @res");
    execute("res");
    EXPECT_EQ(valueToString(vm.getStackTop()), "failed");
    
    // Check if error "oops" is on stack?
    // & pops state, pushes to data.
    // So [oops] should be on data stack BEFORE [failed] @res?
    // [failed] @res pushes "failed", assigns to res.
    // [oops] remains?
    // Stack: [oops].
    // Then res $. Stack: [oops, failed].
    
    // Let's verify stack size/content
    // EXPECT_EQ(stack.size(), 2);
    // EXPECT_EQ(stack[0], "oops");
}

TEST_F(VMStackTest, InvalidComposedDereferenceThrowsAndCatchRecovers) {
    execute("\"first\" @name \"second\" @name -name- & [ok] @res | [caught] @res");
    execute("res");
    EXPECT_EQ(valueToString(vm.getStackTop()), "caught");
}

TEST_F(VMStackTest, SpeculateLiteralReturnsResultWithoutChangingBaseline) {
    execute("\"baseline\" speculate [\"trial\"]");

    auto speculative = vm.popData();
    ASSERT_TRUE(speculative);
    EXPECT_EQ(valueToString(speculative), "trial");

    auto baseline = vm.popData();
    ASSERT_TRUE(baseline);
    EXPECT_EQ(valueToString(baseline), "baseline");
}

TEST_F(VMStackTest, SpeculateFailureReturnsNullWithoutChangingBaseline) {
    execute("\"baseline\" speculate [swap]");

    auto speculative = vm.popData();
    ASSERT_TRUE(speculative);
    EXPECT_TRUE((speculative->getFlags() & agentc::LtvFlags::Null) != agentc::LtvFlags::None);

    auto baseline = vm.popData();
    ASSERT_TRUE(baseline);
    EXPECT_EQ(valueToString(baseline), "baseline");
}
