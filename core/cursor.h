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

#pragma once

#include <string>
#include <functional>
#include "alloc.h"
#include "listree/listree.h"
#include "listree/debug_helpers.h"

namespace agentc {

class Cursor {
private:
    CPtr<ListreeValue> root;
    CPtr<ListreeValue> current;
    CPtr<ListreeItem> currentItem;
    CPtr<ListreeValue> pathComponents; // Slab-allocated list of strings
    bool reverse;
    std::string lastError;
    
    // History Iteration Support
    bool historyMode;
    CPtr<CLL<ListreeValueRef>> historyHead;
    CPtr<CLL<ListreeValueRef>> historyCurrent;
    
    static CPtr<ListreeValue> parsePathString(const std::string& path);
    bool matchPattern(const std::string& pattern, const std::string& name) const;
    void pinPath();
    void unpinPath();
    
public:
    Cursor();
    explicit Cursor(CPtr<ListreeValue> rootValue);
    ~Cursor();
    
    Cursor(const Cursor& other);
    Cursor& operator=(const Cursor& other);
    
    bool resolve(const std::string& path, bool insert = false);
    bool next();
    bool prev();
    bool up();
    bool down();
    
    bool find(const std::string& pattern);
    bool filter(const std::function<bool(const Cursor&)>& predicate);
    
    CPtr<ListreeValue> getValue() const;
    template<typename T> T* getValueAs() const;
    CPtr<ListreeItem> getItem() const;
    std::string getName() const;
    std::string getPath() const;
    bool isValid() const;
    bool isListMode() const;
    
    bool assign(CPtr<ListreeValue> value);
    template<typename T> bool assignValue(const T& value);
    bool remove();
    bool create(const std::string& name, CPtr<ListreeValue> value = nullptr);
    
    bool push(CPtr<ListreeValue> value, bool atEnd = true);
    CPtr<ListreeValue> pop(bool fromEnd = false);
    
    bool forEach(const std::function<bool(Cursor&)>& callback);
    bool forEachChild(const std::function<bool(Cursor&)>& callback);
    void traverse(const std::function<void(CPtr<ListreeValue>)>& callback, TraversalOptions options = {}, std::shared_ptr<TraversalContext> context = nullptr);
    
    void reset(CPtr<ListreeValue> newRoot = nullptr);
    Cursor clone() const;
    const std::string& getLastError() const;
    
    static Cursor createEmpty();
    static Cursor createList();
    static Cursor createFromString(const std::string& str);
    
    friend std::ostream& operator<<(std::ostream& os, const Cursor& cursor);
};

template<typename T>
T* Cursor::getValueAs() const {
    if (!current) return nullptr;
    return static_cast<T*>(current->getData());
}

template<typename T>
bool Cursor::assignValue(const T& value) {
    CPtr<ListreeValue> ltv;
    {
        SlabId sid = Allocator<ListreeValue>::getAllocator().allocate();
        ltv = CPtr<ListreeValue>(sid);
        T* data = new T(value);
        new (Allocator<ListreeValue>::getAllocator().getPtr(sid)) ListreeValue(data, sizeof(T), LtvFlags::Duplicate | LtvFlags::Own);
    }
    return assign(ltv);
}

} // namespace agentc
