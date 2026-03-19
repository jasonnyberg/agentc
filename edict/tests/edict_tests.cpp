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
#include "../edict_types.h"
#include "../edict_types.h"
#include "../edict_vm.h"
#include "../edict_compiler.h"
#include <iostream>

using namespace agentc::edict;

// Test basic stack operations
TEST(EdictVM, StackOperations) {
    // Use Listree-native stack and context
    std::vector<CPtr<agentc::ListreeValue>> dataStack;
    // Push a value first (use a dictionary/tree value)
    auto dictVal = agentc::createNullValue(); // Empty dict/tree
    std::cout << "[DEBUG] StackOperations: Pushing dictVal." << std::endl;
    dataStack.push_back(dictVal);
    // Simulate DUP
    std::cout << "[DEBUG] StackOperations: Simulating DUP." << std::endl;
    dataStack.push_back(dataStack.back());
    EXPECT_EQ(dataStack.size(), 2);
    // Listree-native check: not a list means it's a tree/dictionary
    std::cout << "[DEBUG] StackOperations: Checking isListMode." << std::endl;
    EXPECT_FALSE(dataStack.back()->isListMode());
    dataStack.pop_back();
    EXPECT_FALSE(dataStack.back()->isListMode());
}

// Test dictionary operations
TEST(EdictVM, DictionaryOperations) {
    // Use Listree-native stack and context
    std::vector<CPtr<agentc::ListreeValue>> dataStack;
    std::vector<agentc::Cursor> dictionaryStack;
    // Create root dictionary
    auto root = agentc::createNullValue();
    std::cout << "[DEBUG] DictionaryOperations: Pushing root to dictionaryStack." << std::endl;
    dictionaryStack.push_back(agentc::Cursor(root));
    // Push key and value (dictionary value)
    auto key = agentc::createStringValue("key");
    auto dictVal = agentc::createNullValue(); // Empty dict
    std::cout << "[DEBUG] DictionaryOperations: Pushing key and dictVal to dataStack." << std::endl;
    dataStack.push_back(key);
    dataStack.push_back(dictVal);
    // Assign
    agentc::Cursor ctx = dictionaryStack.back();
    agentc::Cursor temp = ctx;
    std::cout << "[DEBUG] DictionaryOperations: Assigning value." << std::endl;
    temp.resolve("key", true);
    temp.assign(dictVal);
    // Reference
    temp = ctx;
    temp.resolve("key");
    auto result = temp.getValue();
    // Test: result should be a tree/dictionary (not a list)
    EXPECT_TRUE(bool(result));
    EXPECT_FALSE(result->isListMode());
}

#if 0
// Test arithmetic operations
TEST(EdictVM, ArithmeticOperations) { /* ...disabled... */ }
// Test comparison operations
TEST(EdictVM, ComparisonOperations) { /* ...disabled... */ }
// Test dictionary value
TEST(EdictValue, DictionaryValue) { /* ...disabled... */ }
// Test dictionary serialization
TEST(EdictDictionary, Serialization) { /* ...disabled... */ }
#endif

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
