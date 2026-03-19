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
#include "listree.h"

using namespace agentc;

TEST(ListreeTest, DeepCopyList) {
    auto root = createListValue();
    addListItem(root, createStringValue("A"));
    auto copy = root->copy();
    ASSERT_TRUE(copy->isListMode());
    auto copyA = copy->get(true, false);
    EXPECT_EQ(std::string((char*)copyA->getData(), copyA->getLength()), "A");
}
