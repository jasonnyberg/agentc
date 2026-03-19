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
#include "../core/cursor.h"

using namespace agentc;

TEST(CursorTest, ForEachList) {
    auto cursor = Cursor::createList();
    cursor.push(createStringValue("1"));
    std::vector<std::string> visited;
    cursor.forEach([&](Cursor& child) {
        visited.push_back(std::string((char*)child.getValue()->getData(), child.getValue()->getLength()));
        return true;
    });
    EXPECT_EQ(visited.size(), 1);
    EXPECT_EQ(visited[0], "1");
}
