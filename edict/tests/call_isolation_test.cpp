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

class CallIsolationTest : public ::testing::Test {
protected:
    EdictVM vm;
    EdictCompiler compiler;

    void SetUp() override {
        vm.reset();
    }

    int execute(const std::string& code) {
        BytecodeBuffer bc = compiler.compile(code);
        return vm.execute(bc);
    }
};

// Test 1: Stack Isolation
// A function should not be able to "see" or "pop" items from the parent stack
// when called via the f(args) syntax.
TEST_F(CallIsolationTest, StackIsolation) {
    // Define a function that tries to pop an item
    execute("[pop] @f");
    
    // Push an item to the parent stack
    execute("[parent_item]");
    
    // Call f with its own arguments
    // f() should isolate, evaluate its empty args, and then run 'pop'.
    // Since the frame is isolated, 'pop' should fail or at least NOT pop 'parent_item'.
    execute("f()");
    
    // Check stack
    auto stack = stackStrings(vm.dumpStack());
    ASSERT_EQ(stack.size(), 1);
    EXPECT_EQ(stack.back(), "parent_item");
}

// Test 2: Local Scope Isolation
// Variables defined inside the parenthesis should be local to that frame.
TEST_F(CallIsolationTest, LocalScopeIsolation) {
    // f() does nothing
    execute("[] @f");
    
    // Call f and define 'x' inside the isolated argument frame
    // f([hello] @x)
    execute("f([hello] @x)");
    
    // 'x' should NOT be defined in the parent dictionary
    execute("x");
    auto stack = stackStrings(vm.dumpStack());
    ASSERT_FALSE(stack.empty());
    // Undefined symbols resolve to their own name
    EXPECT_EQ(stack.back(), "x");
}

// Test 3: Result Merging
// Items left on the isolated stack should be merged back to the parent.
TEST_F(CallIsolationTest, ResultMerging) {
    // f() just returns its argument
    // Since f is evaluated, if we pass [result], it returns "result"
    execute("[!] @f");
    
    execute("f([result])");
    
    auto stack = stackStrings(vm.dumpStack());
    ASSERT_EQ(stack.size(), 1);
    EXPECT_EQ(stack.back(), "result");
}

// Test 4: Nested Calls (Isolation verification)
// Verify that multiple levels of isolation work as intended.
// Note: If f() is used, it creates an isolated frame. 
// If f uses opcodes that act on the stack (like dup), it will only see its own empty stack.
TEST_F(CallIsolationTest, NestedCalls) {
    // 1. Define f as a thunk that duplicates
    execute("[dup] @f");
    
    // 2. Define g as a thunk that runs f!
    execute("[f!] @g"); 
    
    // 3. Run g([nested])
    // Isolated frame for g gets [nested].
    // runs f! -> resolves f to [dup] -> runs [dup] on g's stack.
    // g's stack becomes [nested, nested].
    // FUN_POP merges.
    execute("g([nested])");
    
    auto stack = stackStrings(vm.dumpStack());
    ASSERT_EQ(stack.size(), 2);
    EXPECT_EQ(stack[0], "nested");
    EXPECT_EQ(stack[1], "nested");
}

// Test 5: One-Shot List Assignment [^x^](1 2 3)
// Verify that the ^ operator can be used to capture the current stack into a dictionary node.
TEST_F(CallIsolationTest, OneShotListAssignment) {
    // Define the "one-shot list assignment" lambda.
    // 1. [] @x : Create a fresh list and assign to local 'x'
    // 2. $x    : Push the list node back to stack
    // 3. ^     : Splice current stack into that list
    execute("[[[] @x $x ^]] @capture");
    
    // Execute: capture(1 2 3)
    // capture starts an isolated frame, evaluates 1 2 3.
    // then runs thunk: [] @x $x ^.
    // [] @x creates local x. $x pushes it. ^ splices 1 2 3 into it.
    // Result: x is [1, 2, 3]. 
    // FUN_POP merges x to parent.
    execute("capture(1 2 3)");
    
    auto stack = stackStrings(vm.dumpStack());
    ASSERT_FALSE(stack.empty());
    // The merged result should be the list node [1, 2, 3]
    EXPECT_EQ(stack.back(), "<list:3>");

    auto result = vm.popData();
    ASSERT_TRUE(result);
    ASSERT_TRUE(result->isListMode());

    std::vector<int> values;
    result->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        ASSERT_TRUE(ref);
        auto value = ref->getValue();
        ASSERT_TRUE(value);
        values.push_back(decodeIntLikeValue(value));
    });

    ASSERT_EQ(values.size(), 3u);
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 3);
}
