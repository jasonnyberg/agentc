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
#include "../mapper.h"
#include <fstream>
#include <iostream>
#include <filesystem>

using namespace agentc::cartographer;
using namespace agentc;

TEST(MapperTest, ParseHeader) {
    Mapper mapper;
    std::filesystem::path testFile = std::filesystem::path(__FILE__).parent_path() / "test_input.h";
    std::string absPath = std::filesystem::absolute(testFile).string();
    CPtr<ListreeValue> root = mapper.parse(absPath);
    ASSERT_TRUE(bool(root));
    Cursor cursor(root);
    ASSERT_TRUE(cursor.resolve("Point"));
    auto typeItem = cursor.getValue()->find("type"); ASSERT_TRUE(bool(typeItem));
    EXPECT_EQ(std::string((char*)typeItem->getValue()->getData(), typeItem->getValue()->getLength()), "struct Point");
}
