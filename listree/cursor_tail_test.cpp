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
#include "../core/cursor.h"
#include <iostream>
#include <cassert>

using namespace agentc;

void test_tail_dereference() {
    std::cout << "Testing Cursor Tail Dereference..." << std::endl;

    // 1. Setup Root
    CPtr<ListreeValue> root = createNullValue(); // Dictionary mode (Tree)
    
    // 2. Create List [ "one", "two", "three" ]
    CPtr<ListreeValue> list = createListValue();
    list->put(createStringValue("one"));
    list->put(createStringValue("two"));
    CPtr<ListreeValue> tailVal = createStringValue("three");
    list->put(tailVal);
    
    // 3. Add List to Root
    addNamedItem(root, "list", list);
    
    // 4. Test Standard Resolve
    {
        Cursor c(root);
        assert(c.resolve("list"));
        assert(c.getValue() == list);
        std::cout << "  Standard resolve passed." << std::endl;
    }
    
    // 5. Test Tail Resolve ("list-")
    {
        Cursor c(root);
        bool res = c.resolve("list-");
        if (!res) {
            std::cerr << "  Tail resolve failed!" << std::endl;
            exit(1);
        }
        
        CPtr<ListreeValue> val = c.getValue();
        // Should be the node containing "three"
        // Wait, ListreeValue::put wraps the value in a ListreeValueRef node? 
        // No, put stores the value ref in the CLL node.
        // get(false, true) returns the *value* stored in the tail node.
        
        // Let's verify the value
        std::string s(static_cast<char*>(val->getData()), val->getLength());
        std::cout << "  Tail value: " << s << std::endl;
        
        assert(s == "three");
        assert(val == tailVal);
        std::cout << "  Tail resolve passed." << std::endl;
    }
}

int main() {
    test_tail_dereference();
    return 0;
}
