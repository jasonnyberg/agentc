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

#include "cursor.h"
#include <fnmatch.h>
#include <algorithm>

namespace agentc {

Cursor::Cursor() : root(nullptr), current(nullptr), currentItem(nullptr), reverse(false), lastError(), historyMode(false), historyHead(nullptr), historyCurrent(nullptr) {
    pathComponents = createListValue();
    LISTREE_DEBUG_TRACE() << "Cursor default constructor";
}

Cursor::Cursor(CPtr<ListreeValue> rootValue)
    : root(rootValue), current(rootValue), currentItem(nullptr), reverse(false), lastError(), historyMode(false), historyHead(nullptr), historyCurrent(nullptr) {
    pathComponents = createListValue();
    pinPath();
    // LISTREE_DEBUG_INFO() << "Cursor created with root node";
}

Cursor::~Cursor() { unpinPath(); }

namespace {

template <size_t N>
bool tryNamedLookup(CPtr<ListreeValue> current,
                    const char* const (&names)[N],
                    const std::function<bool(CPtr<ListreeItem>&, const char*)>& visitor) {
    for (const char* name : names) {
        CPtr<ListreeItem> item = current->find(name);
        if (item && item->getValue() && visitor(item, name)) {
            return true;
        }
    }
    return false;
}

}

Cursor::Cursor(const Cursor& other)
    : root(other.root), current(other.current), currentItem(other.currentItem), reverse(other.reverse), lastError(other.lastError),
      historyMode(other.historyMode), historyHead(other.historyHead), historyCurrent(other.historyCurrent) {
    pathComponents = other.pathComponents ? other.pathComponents->duplicate() : createListValue();
    pinPath();
    LISTREE_DEBUG_TRACE() << "Cursor copy constructor";
}

Cursor& Cursor::operator=(const Cursor& other) {
    if (this != &other) {
        unpinPath();
        root = other.root; current = other.current; currentItem = other.currentItem;
        pathComponents = other.pathComponents ? other.pathComponents->duplicate() : createListValue();
        reverse = other.reverse;
        lastError = other.lastError;
        historyMode = other.historyMode; historyHead = other.historyHead; historyCurrent = other.historyCurrent;
        pinPath();
    }
    return *this;
}

void Cursor::pinPath() { if (current) current->pin(); }
void Cursor::unpinPath() { if (current) current->unpin(); }

CPtr<ListreeValue> Cursor::parsePathString(const std::string& path) {
    CPtr<ListreeValue> components = createListValue();
    if (path.empty()) return components;
    bool isAbsolute = (path[0] == '.');
    size_t start = isAbsolute ? 1 : 0;
    size_t end = path.find('.', start);
    while (end != std::string::npos) {
        if (end > start) components->put(createStringValue(path.substr(start, end - start)), false);
        start = end + 1; end = path.find('.', start);
    }
    if (start < path.size()) components->put(createStringValue(path.substr(start)), false);
    return components;
}

bool Cursor::matchPattern(const std::string& pattern, const std::string& name) const {
    return fnmatch(pattern.c_str(), name.c_str(), FNM_NOESCAPE) == 0;
}

bool Cursor::resolve(const std::string& path, bool insert) {
    lastError.clear();
    if (!root) return false;
    unpinPath();
    bool isAbsolute = (!path.empty() && path[0] == '.');
    CPtr<ListreeValue> newComponents = parsePathString(path);
    bool pathEmpty = true;
    if (bool(pathComponents)) {
        auto first = pathComponents->get(false, false);
        if (bool(first)) pathEmpty = false;
    }
    if (isAbsolute || pathEmpty) {
        current = root; currentItem = nullptr;
        pathComponents = createListValue();
        newComponents->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (ref && ref->getValue()) pathComponents->put(ref->getValue()->duplicate(), false);
        });
    } else {
        newComponents->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (ref && ref->getValue()) pathComponents->put(ref->getValue()->duplicate(), false);
        });
    }
    bool resolvedAll = true;
    newComponents->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (!resolvedAll || !ref || !ref->getValue()) return;
        CPtr<ListreeValue> componentValue = ref->getValue();
        std::string component(static_cast<char*>(componentValue->getData()), componentValue->getLength());
        // std::cout << "DEBUG: resolve component: " << component << std::endl;
        if (component == "..") {
            if (!up()) resolvedAll = false;
            return;
        }
        if (current->isListMode()) {
            resolvedAll = false;
            return;
        }
        
        std::string name = component;
        bool tailDeref = false;
        bool valueTailDeref = false;
        bool iteratorMode = false;

        if (name.length() > 1 && name[0] == '*') {
            iteratorMode = true;
            name = name.substr(1);
        }
        if (name.length() > 1 && name[0] == '-') {
            tailDeref = true;
            name = name.substr(1);
        }
        if (name.length() > 1 && name.back() == '-') {
            valueTailDeref = true;
            name.pop_back();
        }
        
        CPtr<ListreeItem> item = current->find(name, insert);
        if (!item) {
            resolvedAll = false;
            return;
        }
        
        if (iteratorMode) {
             // Create a new cursor that iterates the history of this item.
             Cursor* iter = new Cursor();
             iter->root = root; // Keep context? Or should root be the item?
             // Actually root is just for reset().
             iter->historyMode = true;
             iter->historyHead = item->values;
             // Initialize historyCurrent to the first valid node (lnk[1] from sentinel)
             if (item->values) {
                 iter->historyCurrent = item->values->lnk[1];
                 // Check if empty (points to self)
                 if (iter->historyCurrent == iter->historyHead) {
                     // Empty list.
                     // Should we return an empty iterator? Yes.
                 }
             }
             
             // Wrap in ListreeValue
             CPtr<ListreeValue> wrapper = createCursorValue(iter);
             
              // Update current to point to this wrapper
              current = wrapper;
              currentItem = nullptr; // Detached
              return;
        }
        
        // If tailDeref is true, we want the TAIL (fromEnd=true).
        // Standard (tailDeref=false) gets HEAD (fromEnd=false).
        bool fromEnd = reverse ? !tailDeref : tailDeref;
        
        CPtr<ListreeValue> next = item->getValue(false, fromEnd);

        if (next && valueTailDeref) {
            if (!next->isListMode()) {
                lastError = std::string("Invalid list-tail dereference for '") + component + "'";
                resolvedAll = false;
                return;
            }
            next = next->get(false, true);
        }
         
        // Auto-Creation Logic: If we are inserting and the item has no value (next is null),
        // we must create an intermediate node (empty Tree) to continue traversal.
        // But only if we are NOT at the last component?
        // Actually, if we are resolving a path, we expect to traverse INTO it.
        // If it's the last component, 'next' will be the result of resolve.
        // If 'next' is null, resolve fails (returns false).
        // If 'insert' is true, resolve should PROBABLY return the (possibly empty) item?
        // But 'resolve' updates 'current' to be the VALUE.
        // If we want to assign to 'a.b', 'resolve' should return 'b's value?
        // Or should it return 'b's item?
        // op_ASSIGN calls ctx.assign(v). assign updates 'currentItem'.
        // So resolve MUST leave us at the target item.
        
        if (!next && insert) {
            // Create a new empty Tree (Dictionary) node
            next = createNullValue(); 
            // Add it to the item
            item->addValue(next, false);
        }
        
        if (!next) {
            resolvedAll = false;
            return;
        }
        
        current = next; currentItem = item;
    });
    pinPath(); return resolvedAll;
}

const std::string& Cursor::getLastError() const {
    return lastError;
}

bool Cursor::next() {
    if (historyMode) {
        if (!historyCurrent) return false;
        // Circular list check
        CPtr<CLL<ListreeValueRef>> nextNode = historyCurrent->lnk[1];
        // If we circle back to the head (sentinel), we are done.
        // Assuming historyHead points to the ListreeItem's values sentinel.
        if (nextNode == historyHead) return false;
        historyCurrent = nextNode;
        return true;
    }
    if (!current || !currentItem) return false;
    auto lastVal = pathComponents->get(false, false); if (!lastVal) return false;
    std::string currentName(static_cast<char*>(lastVal->getData()), lastVal->getLength());
    unpinPath();
    std::string parentPath = getPath();
    size_t lastDot = parentPath.find_last_of('.');
    parentPath = (lastDot != std::string::npos) ? parentPath.substr(0, lastDot) : ".";
    CPtr<ListreeValue> sCur = current; CPtr<ListreeItem> sItem = currentItem; CPtr<ListreeValue> sPath = pathComponents->duplicate();
    if (!resolve(parentPath)) { current = sCur; currentItem = sItem; pathComponents = sPath; pinPath(); return false; }
    if (current->isListMode()) { current = sCur; currentItem = sItem; pathComponents = sPath; pinPath(); return false; }

    // H6: Find the in-order successor by collecting all sibling names from the parent tree.
    // This correctly handles multi-character keys (the old nextName.back()++ was single-char only).
    std::string nextName;
    bool foundCurrent = false;
    current->forEachTree([&](const std::string& name, CPtr<ListreeItem>& item) {
        if (nextName.empty() && foundCurrent && item && item->getValue()) {
            nextName = name;
        }
        if (name == currentName) foundCurrent = true;
    });

    if (!nextName.empty()) {
        CPtr<ListreeItem> nextItem = current->find(nextName);
        if (nextItem && nextItem->getValue()) {
            current = nextItem->getValue(); currentItem = nextItem;
            pathComponents = sPath; pathComponents->get(true, false); pathComponents->put(createStringValue(nextName), false);
            pinPath(); return true;
        }
    }
    current = sCur; currentItem = sItem; pathComponents = sPath; pinPath(); return false;
}

bool Cursor::prev() {
    if (!current || !currentItem) return false;
    auto lastVal = pathComponents->get(false, false); if (!lastVal) return false;
    std::string currentName(static_cast<char*>(lastVal->getData()), lastVal->getLength());
    unpinPath();
    std::string parentPath = getPath();
    size_t lastDot = parentPath.find_last_of('.');
    parentPath = (lastDot != std::string::npos) ? parentPath.substr(0, lastDot) : ".";
    CPtr<ListreeValue> sCur = current; CPtr<ListreeItem> sItem = currentItem; CPtr<ListreeValue> sPath = pathComponents->duplicate();
    if (!resolve(parentPath)) { current = sCur; currentItem = sItem; pathComponents = sPath; pinPath(); return false; }
    if (current->isListMode()) { current = sCur; currentItem = sItem; pathComponents = sPath; pinPath(); return false; }

    // H6: Find the in-order predecessor by collecting all sibling names from the parent tree
    // (reverse traversal via forward=false gives descending order; first match after current is predecessor).
    std::string prevName;
    bool foundCurrent = false;
    current->forEachTree([&](const std::string& name, CPtr<ListreeItem>& item) {
        if (prevName.empty() && foundCurrent && item && item->getValue()) {
            prevName = name;
        }
        if (name == currentName) foundCurrent = true;
    }, /*forward=*/false);

    if (!prevName.empty()) {
        CPtr<ListreeItem> prevItem = current->find(prevName);
        if (prevItem && prevItem->getValue()) {
            current = prevItem->getValue(); currentItem = prevItem;
            pathComponents = sPath; pathComponents->get(true, false); pathComponents->put(createStringValue(prevName), false);
            pinPath(); return true;
        }
    }
    current = sCur; currentItem = sItem; pathComponents = sPath; pinPath(); return false;
}

bool Cursor::up() {
    // Guard: peek tail returns nullptr (falsy CPtr) when path is empty — safe to return early.
    if (!pathComponents || !pathComponents->get(false, true)) return false; // Peek tail
    unpinPath(); pathComponents->get(true, true); // Pop tail — only reached after non-null peek above
    std::string newPath = getPath();
    if (resolve(newPath)) return true;
    pinPath(); return false;
}

bool Cursor::down() {
    if (!current) return false;
    unpinPath();
    if (current->isListMode()) {
        CPtr<ListreeValue> child = current->get(false, reverse);
        if (!child) { pinPath(); return false; }
        current = child; currentItem = nullptr;
        pathComponents->put(createStringValue("0"), false);
        pinPath(); return true;
    } else {
        // M7: Descend to the lexicographically first child in tree order.
        // The old hardcoded list {"first", "0", "a", "start", "begin"} was fragile.
        bool found = false;
        current->forEachTree([&](const std::string& name, CPtr<ListreeItem>& item) {
            if (found || !item || !item->getValue()) return;
            current = item->getValue(); currentItem = item;
            pathComponents->put(createStringValue(name), false);
            pinPath();
            found = true;
        }, /*forward=*/!reverse);
        if (!found) { pinPath(); return false; }
        return true;
    }
}

bool Cursor::find(const std::string& pattern) {
    if (!current || current->isListMode()) return false;
    unpinPath();
    CPtr<ListreeValue> sCur = current; CPtr<ListreeItem> sItem = currentItem; CPtr<ListreeValue> sPath = pathComponents->duplicate();
    static const char* const names[] = {"first", "0", "a", "start", "begin", "name", "id", "key", "value"};
    if (tryNamedLookup(current, names, [&](CPtr<ListreeItem>& item, const char* name) {
        if (!matchPattern(pattern, name)) return false;
        current = item->getValue(); currentItem = item;
        pathComponents->put(createStringValue(name), false);
        pinPath();
        return true;
    })) return true;
    current = sCur; currentItem = sItem; pathComponents = sPath; pinPath(); return false;
}

bool Cursor::filter(const std::function<bool(const Cursor&)>& predicate) {
    if (!current || current->isListMode()) return false;
    unpinPath();
    CPtr<ListreeValue> sCur = current; CPtr<ListreeItem> sItem = currentItem; CPtr<ListreeValue> sPath = pathComponents->duplicate();
    static const char* const names[] = {"first", "0", "a", "start", "begin", "name", "id", "key", "value"};
    if (tryNamedLookup(current, names, [&](CPtr<ListreeItem>& item, const char* name) {
        Cursor temp = *this; temp.unpinPath(); temp.current = item->getValue(); temp.currentItem = item;
        temp.pathComponents->put(createStringValue(name), false);
        if (!predicate(temp)) return false;
        current = temp.current; currentItem = temp.currentItem; pathComponents->put(createStringValue(name), false); pinPath();
        return true;
    })) return true;
    current = sCur; currentItem = sItem; pathComponents = sPath; pinPath(); return false;
}

CPtr<ListreeValue> Cursor::getValue() const {
    if (historyMode) {
        if (!historyCurrent || !historyCurrent->data) return nullptr;
        return historyCurrent->data->getValue();
    }
    return current; 
}
CPtr<ListreeItem> Cursor::getItem() const { return currentItem; }
std::string Cursor::getName() const { return currentItem ? currentItem->getName() : ""; }
std::string Cursor::getPath() const {
    std::string path = "."; bool first = true;
    if (pathComponents) {
        pathComponents->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (ref && ref->getValue()) { if (!first) path += "."; auto v = ref->getValue(); path += std::string(static_cast<char*>(v->getData()), v->getLength()); first = false; }
        });
    }
    return path;
}
bool Cursor::isValid() const { return bool(current); }
bool Cursor::isListMode() const { return current && current->isListMode(); }
bool Cursor::assign(CPtr<ListreeValue> value) { 
    if (!current || !currentItem) return false;
    // Refuse writes to read-only branches.  Because setReadOnly(recursive=true)
    // marks all descendants, current->isReadOnly() is true for any value inside
    // a frozen tree.
    if (current->isReadOnly()) {
        LOG_CURSOR("assign: write refused on read-only node");
        return false;
    }
    
    // Check if the current head value is an empty placeholder (Null flag and no data/children)
    // This happens when resolve(..., true) creates intermediate nodes.
    bool isPlaceholder = false;
    CPtr<ListreeValue> head = currentItem->getValue(false, false);
    if (head) {
        bool isNull = (head->getFlags() & LtvFlags::Null) != LtvFlags::None;
        bool hasData = head->getData() != nullptr || head->getLength() > 0;
        
        bool hasChildren = false;
        if (head->isListMode()) {
             // Check if list empty?
             // But createNullValue() makes a Tree mode LTV.
        } else {
             head->forEachTree([&](const std::string&, CPtr<ListreeItem>&){ hasChildren = true; });
        }
        
        if (isNull && !hasData && !hasChildren) {
            isPlaceholder = true;
        }
    }
    
    if (isPlaceholder) {
        // Pop the placeholder
        currentItem->getValue(true, false);
    }
    
    // Accumulate value
    currentItem->addValue(value, false); // Add to head
    current = value; 
    pinPath(); 
    return true; 
}
bool Cursor::remove() {
    if (!current || !currentItem || !pathComponents) return false;
    
    auto lastVal = pathComponents->get(false, false); if (!lastVal) return false; // Peek tail (false=Tail)
    std::string name(static_cast<char*>(lastVal->getData()), lastVal->getLength());
    
    // 1. Pop the value from the current item
    CPtr<ListreeValue> popped = currentItem->getValue(true, reverse); 
    if (!popped) return false;
    
    // 2. Recursive Cleanup
    while (true) {
        // Check if current item still has values
        if (currentItem->getValue(false, false)) break; // Not empty, stop cleanup.
        if (currentItem->getValue(false, false)) break; // Not empty, stop cleanup.
        
        // Item is empty. We need to remove it from its parent.
        std::string nameToRemove = currentItem->getName();
        
        // Move up to parent
        if (!up()) {
            // ...
            if (current && !current->isListMode()) {
                current->remove(nameToRemove);
            }
            break;
        }
        
        // We moved up. 'current' is the Parent Tree. 'currentItem' is the Parent Item.
        // Remove the child item from 'current'.
        if (current && !current->isListMode()) {
            current->remove(nameToRemove);
        } else {
        }
        
        // If we are at the root (currentItem is null), we are done.
        if (!currentItem) break;
        
        // Now check if 'current' (the Parent Tree) is empty.
        // A ListreeValue is empty if it has no data and no items (if tree).
        // isEmpty() checks flags/data. But we care about 'tree' being empty.
        // Actually, we care if it is a "useless placeholder".
        // If it has data (string/binary), keep it.
        // If it has other items, keep it.
        // Helper: does it have children?
        bool hasChildren = false;
        current->forEachTree([&](const std::string&, CPtr<ListreeItem>&){ hasChildren = true; });
        
        if (!current->isEmpty() || hasChildren) {
            break; // Parent tree is not empty/useless. Stop.
        }
        
        // Parent Tree is empty/useless.
        // It is a value of 'currentItem' (Grandparent Item).
        // We loop back to top: check if 'currentItem' (Grandparent Item) is empty...
        // Wait, we need to remove 'current' (the empty tree) from 'currentItem'.
        // 'currentItem'->getValue() returned 'current'.
        // So we pop 'current' from 'currentItem'.
        // But 'currentItem' might have *other* values (stack).
        // We should only remove 'current' if it's the specific value we just emptied.
        // But 'up()' sets 'current' to the value.
        // We need to remove *that specific value* from 'currentItem'.
        // 'currentItem' values list. Remove 'current'?
        // 'ListreeItem' doesn't support "remove specific value pointer".
        // It supports pop head/tail.
        // Assumption: The structure was created via hierarchical assignment, so it's likely the head value.
        // Or we can just pop head?
        
        // Simpler approach for now:
        // Just pop the head of 'currentItem'.
        currentItem->getValue(true, reverse); // Pop the empty tree value.
        
        // Loop continues to check if 'currentItem' is now empty.
    }
    
    // Restore valid state? 
    // remove() invalidates the cursor's current position if it deleted the item/value.
    // The cursor is now pointing at... wherever the loop stopped (parent or root).
    // This is acceptable for 'remove'.
    pinPath();
    return true;
}
bool Cursor::create(const std::string& name, CPtr<ListreeValue> value) {
    if (!current || current->isListMode()) return false;
    unpinPath(); if (!value) value = createNullValue();
    CPtr<ListreeItem> item = current->find(name, true); if (!item) { pinPath(); return false; }
    item->addValue(value, false); current = value; currentItem = item;
    pathComponents->put(createStringValue(name), false); pinPath(); return true;
}
bool Cursor::push(CPtr<ListreeValue> value, bool atEnd) { if (!current || !current->isListMode()) return false; return bool(current->put(value, atEnd)); }
CPtr<ListreeValue> Cursor::pop(bool fromEnd) { if (!current || !current->isListMode()) return nullptr; return current->get(true, fromEnd); }
bool Cursor::forEach(const std::function<bool(Cursor&)>& callback) {
    if (!current) return false;
    bool cont = true;
    if (current->isListMode()) {
        current->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (!cont || !ref || !ref->getValue()) return;
            Cursor child = *this; child.unpinPath(); child.current = ref->getValue(); child.currentItem = nullptr;
            child.pathComponents->put(createStringValue("0"), false); child.pinPath();
            if (!callback(child)) cont = false;
        });
    } else {
        current->forEachTree([&](const std::string& name, CPtr<ListreeItem>& item) {
            if (!cont || !item) return;
            item->forEachValue([&](CPtr<ListreeValue>& val) {
                if (!cont || !val) return;
                Cursor child = *this; child.unpinPath(); child.current = val; child.currentItem = item;
                child.pathComponents->put(createStringValue(name), false); child.pinPath();
                if (!callback(child)) cont = false;
            });
        });
    }
    return cont;
}
bool Cursor::forEachChild(const std::function<bool(Cursor&)>& callback) { return forEach(callback); }
void Cursor::traverse(const std::function<void(CPtr<ListreeValue>)>& callback, TraversalOptions options, std::shared_ptr<TraversalContext> context) { if (current) { if (!options.from) options.from = current; current->traverse(callback, options, context); } }
void Cursor::reset(CPtr<ListreeValue> newRoot) { unpinPath(); if (newRoot) root = newRoot; current = root; currentItem = nullptr; pathComponents = createListValue(); pinPath(); }
Cursor Cursor::clone() const { return *this; }
Cursor Cursor::createEmpty() { return Cursor(createNullValue()); }
Cursor Cursor::createList() { return Cursor(createListValue()); }
Cursor Cursor::createFromString(const std::string& str) { return Cursor(createStringValue(str)); }
std::ostream& operator<<(std::ostream& os, const Cursor& cursor) {
    os << "Cursor[path='" << cursor.getPath() << "', valid=" << (cursor.isValid() ? "true" : "false");
    if (cursor.currentItem) os << ", name='" << cursor.getName() << "'";
    if (cursor.current) os << ", mode='" << (cursor.isListMode() ? "list" : "tree") << "'";
    os << "]"; return os;
}

} // namespace agentc
