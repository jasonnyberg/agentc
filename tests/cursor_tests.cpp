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
#include "core/cursor.h"
#include "core/debug.h"

// Test fixture for Cursor tests
class CursorTest : public ::testing::Test {
protected:
    CPtr<AATree<int>> root;
    
    void SetUp() override {
        // Create a test tree structure
        root = CPtr<AATree<int>>();
        root->name = "root";
        
        // Add some nodes to the tree
        CPtr<int> value1(42);
        CPtr<int> value2(84);
        CPtr<int> value3(126);
        
        root->add("node1", value1);
        root->add("node2", value2);
        root->add("node3", value3);
        
        // Add some nested nodes
        CPtr<AATree<int>> node1 = root->find("node1");
        CPtr<int> value4(168);
        CPtr<int> value5(210);
        
        node1->add("child1", value4);
        node1->add("child2", value5);
    }
};

// Test basic cursor creation
TEST_F(CursorTest, Creation) {
    Cursor<int> cursor(root);
    EXPECT_TRUE(cursor.isValid());
    EXPECT_EQ(cursor.getName(), "root");
    EXPECT_EQ(cursor.getPath(), "");
}

// Test path resolution
TEST_F(CursorTest, PathResolution) {
    Cursor<int> cursor(root);
    
    // Resolve a direct child
    EXPECT_TRUE(cursor.resolve("node1"));
    EXPECT_EQ(cursor.getName(), "node1");
    EXPECT_EQ(cursor.getPath(), "node1");
    EXPECT_EQ(*cursor.getValue(), 42);
    
    // Resolve a nested path
    EXPECT_TRUE(cursor.resolve("node1.child1"));
    EXPECT_EQ(cursor.getName(), "child1");
    EXPECT_EQ(cursor.getPath(), "node1.child1");
    EXPECT_EQ(*cursor.getValue(), 168);
    
    // Resolve an absolute path
    EXPECT_TRUE(cursor.resolve(".node2"));
    EXPECT_EQ(cursor.getName(), "node2");
    EXPECT_EQ(cursor.getPath(), "node2");
    EXPECT_EQ(*cursor.getValue(), 84);
    
    // Try to resolve a non-existent path
    EXPECT_FALSE(cursor.resolve("nonexistent"));
}

// Test navigation
TEST_F(CursorTest, Navigation) {
    Cursor<int> cursor(root);
    
    // Navigate to a node
    EXPECT_TRUE(cursor.resolve("node1"));
    
    // Navigate up
    EXPECT_TRUE(cursor.up());
    EXPECT_EQ(cursor.getName(), "root");
    
    // Navigate down (not fully implemented yet)
    // EXPECT_TRUE(cursor.down());
    // EXPECT_EQ(cursor.getName(), "node1");
    
    // Navigate to a nested node
    EXPECT_TRUE(cursor.resolve("node1.child1"));
    
    // Navigate up one level
    EXPECT_TRUE(cursor.up());
    EXPECT_EQ(cursor.getName(), "node1");
    
    // Navigate up to root
    EXPECT_TRUE(cursor.up());
    EXPECT_EQ(cursor.getName(), "root");
    
    // Try to navigate up from root
    EXPECT_FALSE(cursor.up());
}

// Test value manipulation
TEST_F(CursorTest, ValueManipulation) {
    Cursor<int> cursor(root);
    
    // Navigate to a node
    EXPECT_TRUE(cursor.resolve("node1"));
    EXPECT_EQ(*cursor.getValue(), 42);
    
    // Assign a new value
    CPtr<int> new_value(99);
    EXPECT_TRUE(cursor.assign(new_value));
    EXPECT_EQ(*cursor.getValue(), 99);
    
    // Create a new node through path resolution
    EXPECT_TRUE(cursor.resolve("node4", true));
    EXPECT_EQ(cursor.getName(), "node4");
    
    // Assign a value to the new node
    CPtr<int> another_value(123);
    EXPECT_TRUE(cursor.assign(another_value));
    EXPECT_EQ(*cursor.getValue(), 123);
    
    // Remove a node
    EXPECT_TRUE(cursor.resolve("node4"));
    EXPECT_TRUE(cursor.remove());
    EXPECT_EQ(cursor.getName(), "root");
    
    // Verify the node was removed
    EXPECT_FALSE(cursor.resolve("node4"));
}

// Test value-based filtering
TEST_F(CursorTest, ValueFiltering) {
    Cursor<int> cursor(root);
    
    // Navigate to a node
    EXPECT_TRUE(cursor.resolve("node1"));
    
    // Test filtering with matching value
    EXPECT_TRUE(cursor.filter(42));
    
    // Test filtering with non-matching value
    EXPECT_FALSE(cursor.filter(99));
}

// Test wildcard pattern matching
TEST_F(CursorTest, WildcardMatching) {
    Cursor<int> cursor(root);
    
    // Add some nodes with similar names
    CPtr<int> value6(252);
    CPtr<int> value7(294);
    CPtr<int> value8(336);
    
    root->add("test1", value6);
    root->add("test2", value7);
    root->add("other", value8);
    
    // Test wildcard resolution (not fully implemented yet)
    // EXPECT_TRUE(cursor.resolve("test*"));
    // EXPECT_TRUE(cursor.getName() == "test1" || cursor.getName() == "test2");
    
    // Test next with wildcard (not fully implemented yet)
    // EXPECT_TRUE(cursor.next());
    // EXPECT_TRUE(cursor.getName() == "test1" || cursor.getName() == "test2");
    // EXPECT_FALSE(cursor.next());
}
