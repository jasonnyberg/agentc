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
#include "../core/cursor.h"

using namespace agentc;

TEST(CycleTest, DetectCycle) {
    auto valA = createStringValue("A"); auto valB = createStringValue("B");
    addNamedItem(valA, "toB", valB); addNamedItem(valB, "toA", valA);
    std::vector<std::string> visited;
    valA->traverse([&](CPtr<ListreeValue> val) { if (val && val->getData()) visited.push_back(std::string((char*)val->getData(), val->getLength())); });
    EXPECT_EQ(visited.size(), 2);
}
