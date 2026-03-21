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

TEST(MapperTest, BindTypesCreatesTypeDef) {
    // After parse(), struct Rect has an "origin" field whose type is "struct Point".
    // bindTypes() should have attached a "type_def" child to that field LTV,
    // pointing to the top-level "Point" entry (the same CPtr, no copy).
    Mapper mapper;
    std::filesystem::path testFile = std::filesystem::path(__FILE__).parent_path() / "test_input.h";
    std::string absPath = std::filesystem::absolute(testFile).string();
    CPtr<ListreeValue> root = mapper.parse(absPath);
    ASSERT_TRUE(bool(root));

    // Navigate to Rect.children.origin
    Cursor cursor(root);
    ASSERT_TRUE(cursor.resolve("Rect")) << "Rect not found in namespace";
    ASSERT_TRUE(cursor.resolve("children")) << "Rect has no children node";
    ASSERT_TRUE(cursor.resolve("origin")) << "Rect.children has no origin field";

    // origin field should have a type_def child
    auto typeDef = cursor.getValue()->find("type_def");
    ASSERT_TRUE(bool(typeDef)) << "origin field has no type_def child (bindTypes() failed)";

    // type_def should point to the Point struct — verify by checking its "type" value
    auto typeItem = typeDef->getValue()->find("type");
    ASSERT_TRUE(bool(typeItem)) << "type_def has no 'type' child";
    std::string typeStr((char*)typeItem->getValue()->getData(), typeItem->getValue()->getLength());
    EXPECT_EQ(typeStr, "struct Point");
}
