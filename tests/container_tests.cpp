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
#include "core/container.h"
#include <iostream>

using namespace std;

TEST(CLLTest, Creation) {
    SlabId sid = Allocator<CLL<int>>::getAllocator().allocate();
    CPtr<CLL<int>> list(sid);
    
    // Default CLL has data=nullptr
    EXPECT_FALSE(bool(list->data));
    
    // Link to self
    // We need to access private members for thorough testing or trust public API
    // Since lnk is private, we rely on public methods like get/put
}

TEST(CLLTest, StoreAndRetrieve) {
    // Create root (sentinel)
    SlabId rootSid = Allocator<CLL<int>>::getAllocator().allocate();
    CPtr<CLL<int>> root(rootSid);
    
    // Create Item 1
    SlabId item1Sid = Allocator<CLL<int>>::getAllocator().allocate(10);
    CPtr<CLL<int>> item1(item1Sid);
    
    // Store
    root->store(item1);
    
    // Retrieve (get next)
    CPtr<CLL<int>> next = &root->get(true);
    ASSERT_TRUE(bool(next));
    ASSERT_TRUE(bool(next->data));
    EXPECT_EQ(*next->data, 10);
    
    // Remove
    root->remove(true);
}

TEST(AATreeTest, AddAndFind) {
    // Create root
    SlabId rootSid = Allocator<AATree<int>>::getAllocator().allocate();
    CPtr<AATree<int>> root(rootSid);
    
    CPtr<int> val1(100);
    CPtr<int> val2(200);
    
    root->add("key1", val1);
    root->add("key2", val2);
    
    auto found1 = root->find("key1");
    ASSERT_TRUE(bool(found1));
    EXPECT_EQ(*found1->data, 100);
    
    auto found2 = root->find("key2");
    ASSERT_TRUE(bool(found2));
    EXPECT_EQ(*found2->data, 200);
    
    auto notFound = root->find("key3");
    EXPECT_FALSE(bool(notFound));
}