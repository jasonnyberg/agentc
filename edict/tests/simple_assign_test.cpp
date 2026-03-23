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

TEST(SimpleAssignTest, DictAssignment) {
    EdictVM vm;
    std::string script = R"(
        'mykey
        { "a": 1, "b": 2 }
        @
        
        mykey
    )";
    
    vm.execute(EdictCompiler().compile(script));
    
    auto res = vm.popData();
    ASSERT_TRUE(bool(res));
    // JSON objects compile to dictionary/tree mode in j3.
    ASSERT_FALSE(res->isListMode());
}

TEST(SimpleAssignTest, JsonObjectLiteralContainsAssignedKeys) {
    EdictVM vm;
    std::string script = R"(
        { "a": 1, "b": 2 }
    )";

    vm.execute(EdictCompiler().compile(script));

    auto res = vm.popData();
    ASSERT_TRUE(bool(res));
    ASSERT_FALSE(res->isListMode());
    EXPECT_TRUE(bool(res->find("a")));
    EXPECT_TRUE(bool(res->find("b")));
}
