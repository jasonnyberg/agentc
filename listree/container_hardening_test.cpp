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

#include "listree.h"
#include <iostream>
#include <cassert>
#include <vector>

using namespace agentc;

void assert_msg(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "FAIL: " << msg << std::endl;
        exit(1);
    } else {
        std::cout << "PASS: " << msg << std::endl;
    }
}

int main() {
    std::cout << "Running Container Hardening Tests..." << std::endl;

    // Test 1: AATree Null Pointer Recovery (The original bug)
    {
        CPtr<ListreeValue> root = createNullValue(); 
        CPtr<ListreeValue> valA = createStringValue("valA");
        addNamedItem(root, "a", valA);
        root->remove("a");
        
        CPtr<ListreeValue> valB = createStringValue("valB");
        addNamedItem(root, "b", valB);
        
        CPtr<ListreeItem> foundB = root->find("b");
        assert_msg(bool(foundB), "Recovered from null tree after removal");
    }

    // Test 2: Removing non-existent item
    {
        CPtr<ListreeValue> root = createNullValue();
        CPtr<ListreeValue> valA = createStringValue("valA");
        addNamedItem(root, "a", valA);
        
        root->remove("z"); // Should do nothing
        
        CPtr<ListreeItem> foundA = root->find("a");
        assert_msg(bool(foundA), "Existing item 'a' remains after removing non-existent 'z'");
    }

    // Test 3: Removing from empty tree
    {
        CPtr<ListreeValue> root = createNullValue();
        root->remove("x"); // Should be safe
        assert_msg(root->isEmpty() || !root->isListMode(), "Removing from empty tree is safe");
    }

    // Test 4: List Mode safety
    {
        CPtr<ListreeValue> list = createListValue();
        CPtr<ListreeValue> val = createStringValue("item");
        
        // Try to add named item to list - should be ignored or fail gracefully
        addNamedItem(list, "key", val);
        
        // Verify it didn't change mode or crash
        assert_msg(list->isListMode(), "List remains in List Mode after invalid named add");
        // We can't easily check internal state without friend access, but we verified no crash.
    }
    
    // Test 5: Recursive Removal (Child Tree)
    {
        CPtr<ListreeValue> root = createNullValue();
        // create a.b
        CPtr<ListreeValue> valB = createStringValue("valB");
        CPtr<ListreeValue> valA = createNullValue();
        addNamedItem(valA, "b", valB);
        addNamedItem(root, "a", valA);
        
        // Remove 'a'
        root->remove("a");
        assert_msg(!root->find("a"), "Removed subtree 'a'");
    }

    std::cout << "All Hardening Tests Passed." << std::endl;
    return 0;
}