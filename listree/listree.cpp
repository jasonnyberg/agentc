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
#include <cstring>

namespace agentc {

namespace {

CPtr<CLL<ListreeValue>> createTraversalQueue() {
    SlabId sid = Allocator<CLL<ListreeValue>>::getAllocator().allocate();
    return CPtr<CLL<ListreeValue>>(sid);
}

void enqueueTraversalValue(CPtr<CLL<ListreeValue>>& queue, CPtr<ListreeValue> value) {
    if (!queue || !value) return;
    SlabId nodeSid = Allocator<CLL<ListreeValue>>::getAllocator().allocate();
    CPtr<CLL<ListreeValue>> node(nodeSid);
    node->data = value;
    queue->store(node, true);
}

CPtr<ListreeValue> dequeueTraversalValue(CPtr<CLL<ListreeValue>>& queue) {
    if (!queue) return nullptr;
    CPtr<CLL<ListreeValue>> node = &queue->get(true);
    if (!node || !node->data) return nullptr;
    CPtr<ListreeValue> value = node->data;
    queue->remove(true);
    return value;
}

} // namespace

//////////////////////////////////////////////////
// TraversalContext Implementation
//////////////////////////////////////////////////

TraversalContext::TraversalContext() {}
TraversalContext::~TraversalContext() {}
void TraversalContext::mark_absolute(SlabId sid) { absolute_visited.insert(sid); }
bool TraversalContext::is_absolute(SlabId sid) const { return absolute_visited.count(sid) > 0; }
void TraversalContext::mark_recursive(SlabId sid) { recursive_visited.insert(sid); }
void TraversalContext::unmark_recursive(SlabId sid) { recursive_visited.erase(sid); }
bool TraversalContext::is_recursive(SlabId sid) const { return recursive_visited.count(sid) > 0; }
void TraversalContext::clear() { absolute_visited.clear(); recursive_visited.clear(); }

//////////////////////////////////////////////////
// ListreeValueRef Implementation
//////////////////////////////////////////////////

ListreeValueRef::ListreeValueRef() : value(nullptr) {}
ListreeValueRef::ListreeValueRef(CPtr<ListreeValue> value) : value(value) {}
ListreeValueRef::~ListreeValueRef() {}
bool ListreeValueRef::unwind(SlabId sid) { return true; }
std::ostream& operator<<(std::ostream& os, const ListreeValueRef& ltvr) { os << "LTVR(" << ltvr.value << ")"; return os; }

//////////////////////////////////////////////////
// ListreeItem Implementation
//////////////////////////////////////////////////

ListreeItem::ListreeItem() : name("") {
    SlabId sid = Allocator<CLL<ListreeValueRef>>::getAllocator().allocate();
    values = CPtr<CLL<ListreeValueRef>>(sid);
}
ListreeItem::ListreeItem(const std::string& name) : name(name) {
    SlabId sid = Allocator<CLL<ListreeValueRef>>::getAllocator().allocate();
    values = CPtr<CLL<ListreeValueRef>>(sid);
}
ListreeItem::~ListreeItem() {}
CPtr<ListreeValueRef> ListreeItem::addValue(CPtr<ListreeValue>& value, bool atEnd) {
    CPtr<ListreeValueRef> ref;
    { SlabId refSid = Allocator<ListreeValueRef>::getAllocator().allocate(value); ref = CPtr<ListreeValueRef>(refSid); }
    if (values) {
        CPtr<CLL<ListreeValueRef>> node;
        { SlabId nodeSid = Allocator<CLL<ListreeValueRef>>::getAllocator().allocate(); node = CPtr<CLL<ListreeValueRef>>(nodeSid); node->data = ref; }
        values->store(node, atEnd);
    }
    return ref;
}
CPtr<ListreeValue> ListreeItem::getValue(bool pop, bool fromEnd) {
    if (!values) return nullptr;
    CPtr<CLL<ListreeValueRef>> current = &values->get(fromEnd);
    if (!current || !current->data) return nullptr;
    CPtr<ListreeValue> value = current->data->getValue();
    if (pop) values->remove(fromEnd);
    return value;
}
void ListreeItem::forEachValue(const std::function<void(CPtr<ListreeValue>&)>& callback, bool forward) {
    if (values) values->forEach([&](CPtr<ListreeValueRef>& ref) { if (ref) { CPtr<ListreeValue> val = ref->getValue(); callback(val); } }, forward);
}
void ListreeItem::forEachRef(const std::function<void(CPtr<ListreeValueRef>&)>& callback, bool forward) {
    if (values) values->forEach(callback, forward);
}
bool ListreeItem::unwind(SlabId sid) { return true; }
std::ostream& operator<<(std::ostream& os, const ListreeItem& lti) { os << "LTI(name=\"" << lti.name << "\")"; return os; }

//////////////////////////////////////////////////
// ListreeValue Implementation
//////////////////////////////////////////////////

void ListreeValue::initContainer() {
    if (isListMode()) {
        if (!list) {
            SlabId sid = Allocator<CLL<ListreeValueRef>>::getAllocator().allocate();
            list = CPtr<CLL<ListreeValueRef>>(sid);
        }
    } else {
        if (!tree) {
            SlabId sid = Allocator<AATree<ListreeItem>>::getAllocator().allocate();
            tree = CPtr<AATree<ListreeItem>>(sid);
        }
    }
}
ListreeValue::ListreeValue() : list(nullptr), tree(nullptr), flags(LtvFlags::Null), pinnedCount(0) { 
    payload.ext.ptr = nullptr;
    payload.ext.length = 0;
    initContainer(); 
}
ListreeValue::ListreeValue(void* data, size_t length, LtvFlags flags) 
    : list(nullptr), tree(nullptr), flags(flags), pinnedCount(0) {
    bool wantsCopy = ((flags & LtvFlags::Duplicate) != LtvFlags::None) || ((flags & LtvFlags::Free) != LtvFlags::None);
    if (length > 0 && length <= 15 && wantsCopy) {
        this->flags = static_cast<LtvFlags>(static_cast<int>(this->flags) & ~(static_cast<int>(LtvFlags::Duplicate) | static_cast<int>(LtvFlags::Free))) | LtvFlags::Immediate;
        if (data) memcpy(this->payload.sso.bytes, data, length); else memset(this->payload.sso.bytes, 0, length);
        this->payload.sso.length = static_cast<uint8_t>(length);
    } else if (length > 0) {
        if (wantsCopy) {
            SlabId blobId = BlobAllocator::getAllocator().allocate(data, length);
            this->payload.ext.ptr = nullptr;
            memcpy(&this->payload.ext.ptr, &blobId, sizeof(SlabId));
            this->flags = static_cast<LtvFlags>(static_cast<int>(this->flags) & ~(static_cast<int>(LtvFlags::Duplicate) | static_cast<int>(LtvFlags::Free))) | LtvFlags::SlabBlob;
        } else { 
            this->payload.ext.ptr = data; 
        }
        this->payload.ext.length = length;
    } else { this->payload.ext.ptr = nullptr; this->payload.ext.length = 0; }
    initContainer();
}
ListreeValue::ListreeValue(const std::string& str, LtvFlags flags) 
    : list(nullptr), tree(nullptr), flags(flags | LtvFlags::Duplicate), pinnedCount(0) {
    size_t len = str.length();
    if (len > 0 && len <= 15) {
        this->flags = static_cast<LtvFlags>(static_cast<int>(this->flags) & ~(static_cast<int>(LtvFlags::Duplicate) | static_cast<int>(LtvFlags::Free))) | LtvFlags::Immediate;
        memcpy(this->payload.sso.bytes, str.c_str(), len);
        this->payload.sso.length = static_cast<uint8_t>(len);
    } else if (len > 0) {
        SlabId blobId = BlobAllocator::getAllocator().allocate(str.data(), len);
        this->payload.ext.ptr = nullptr;
        memcpy(&this->payload.ext.ptr, &blobId, sizeof(SlabId));
        this->flags = static_cast<LtvFlags>(static_cast<int>(this->flags) & ~(static_cast<int>(LtvFlags::Duplicate) | static_cast<int>(LtvFlags::Free))) | LtvFlags::SlabBlob;
        this->payload.ext.length = len;
    } else { this->payload.ext.ptr = nullptr; this->payload.ext.length = 0; this->flags = this->flags | LtvFlags::Null; }
    initContainer();
}
ListreeValue::~ListreeValue() { 
    if ((flags & LtvFlags::Iterator) != LtvFlags::None && payload.ext.ptr) {
        delete static_cast<Cursor*>(payload.ext.ptr);
        payload.ext.ptr = nullptr;
    } else if (payload.ext.ptr && ((flags & LtvFlags::Free) != LtvFlags::None) && ((flags & LtvFlags::Immediate) == LtvFlags::None)) { 
        free(payload.ext.ptr); 
        payload.ext.ptr = nullptr; 
    } 
}
CPtr<ListreeItem> ListreeValue::find(const std::string& name, bool insert) {
    if (isListMode()) return nullptr;
    if (!tree) {
        if (insert) {
            SlabId sid = Allocator<AATree<ListreeItem>>::getAllocator().allocate();
            tree = CPtr<AATree<ListreeItem>>(sid);
        } else {
            return nullptr;
        }
    }
    CPtr<AATree<ListreeItem>> node = tree->find(name);
    if (!node && insert) {
        CPtr<ListreeItem> item;
        { SlabId itemSid = Allocator<ListreeItem>::getAllocator().allocate(name); item = CPtr<ListreeItem>(itemSid); }
        tree->add(name, item);
        node = tree->find(name);
    }
    return node ? node->data : nullptr;
}
CPtr<ListreeItem> ListreeValue::remove(const std::string& name) {
    if (isListMode() || !tree) return nullptr;
    CPtr<AATree<ListreeItem>> node = tree->find(name);
    if (node) {
        CPtr<ListreeItem> item = node->data;
        bool pinned = false;
        item->forEachValue([&](CPtr<ListreeValue>& val) { if (val && val->getPinnedCount() > 0) pinned = true; });
        if (pinned) {
            return nullptr;
        }
        tree = tree->remove(name);
        while (item->getValue(true, true)) {}
        return item;
    }
    return nullptr;
}
CPtr<ListreeValueRef> ListreeValue::put(CPtr<ListreeValue> value, bool atEnd) {
    initContainer();
    if (!isListMode() || !list) return nullptr;
    CPtr<ListreeValueRef> ref;
    { SlabId refSid = Allocator<ListreeValueRef>::getAllocator().allocate(value); ref = CPtr<ListreeValueRef>(refSid); }
    CPtr<CLL<ListreeValueRef>> node;
    { SlabId nodeSid = Allocator<CLL<ListreeValueRef>>::getAllocator().allocate(); node = CPtr<CLL<ListreeValueRef>>(nodeSid); node->data = ref; }
    list->store(node, atEnd);
    
    return ref;
}
CPtr<ListreeValue> ListreeValue::get(bool pop, bool fromEnd) {
    if (!isListMode() || !list) return nullptr;
    CPtr<CLL<ListreeValueRef>> current = &list->get(fromEnd);
    if (!current || !current->data) return nullptr;
    CPtr<ListreeValue> value = current->data->getValue();
    if (pop) list->remove(fromEnd);
    return value;
}
CPtr<ListreeValue> ListreeValue::duplicate() const { return copy(-1); }
CPtr<ListreeValue> ListreeValue::copy(int maxDepth) const {
    CPtr<ListreeValue> res;
    {
        // Normalize flags for copy: strip SlabBlob/Immediate/Free and use Duplicate
        // so the constructor routes the raw data correctly (SSO or BlobAllocator).
        LtvFlags copyFlags = flags;
        if ((flags & LtvFlags::SlabBlob) != LtvFlags::None || (flags & LtvFlags::Immediate) != LtvFlags::None) {
            copyFlags = static_cast<LtvFlags>(
                (static_cast<int>(flags) & ~(static_cast<int>(LtvFlags::SlabBlob) | static_cast<int>(LtvFlags::Immediate) | static_cast<int>(LtvFlags::Free)))
                | static_cast<int>(LtvFlags::Duplicate));
        }
        SlabId sid = Allocator<ListreeValue>::getAllocator().allocate(getData(), getLength(), copyFlags);
        res = CPtr<ListreeValue>(sid);
    }
    if (maxDepth == 0) return res;
    int nextDepth = (maxDepth > 0) ? maxDepth - 1 : -1;
    if (isListMode() && list) {
        list->forEach([&](CPtr<ListreeValueRef>& ref) { if (ref && ref->getValue()) { CPtr<ListreeValue> childCopy = ref->getValue()->copy(nextDepth); res->put(childCopy, false); } });
    } else if (!isListMode() && tree) {
        tree->forEach([&](const std::string& name, CPtr<ListreeItem>& item) {
            if (item) { CPtr<ListreeItem> copyItem = res->find(name, true); item->forEachValue([&](CPtr<ListreeValue>& val) { if (val) { CPtr<ListreeValue> valCopy = val->copy(nextDepth); copyItem->addValue(valCopy, false); } }); }
        });
    }
    return res;
}
void ListreeValue::forEachList(const std::function<void(CPtr<ListreeValueRef>&)>& callback, bool forward) { if (isListMode() && list) list->forEach(callback, forward); }
void ListreeValue::forEachTree(const std::function<void(const std::string&, CPtr<ListreeItem>&)>& callback, bool forward) { if (!isListMode() && tree) tree->forEach(callback, forward); }

void ListreeValue::traverse(const std::function<void(CPtr<ListreeValue>)>& callback, TraversalOptions options, std::shared_ptr<TraversalContext> context) {
    CPtr<ListreeValue> startNode = options.from;
    if (!startNode) { try { SlabId sid = Allocator<ListreeValue>::getAllocator().getSlabId(this); startNode = CPtr<ListreeValue>(sid); } catch (...) { return; } }
    if (!context) context = std::make_shared<TraversalContext>();
    SlabId sid = startNode.getSlabId();
    if (context->is_recursive(sid) || context->is_absolute(sid)) return;
    bool forward = (options.direction == TraversalDirection::Forward);
    if (options.order == TraversalOrder::BreadthFirst) {
        CPtr<CLL<ListreeValue>> queue = createTraversalQueue();
        enqueueTraversalValue(queue, startNode);
        while (true) {
            CPtr<ListreeValue> current = dequeueTraversalValue(queue);
            if (!current) break;
            SlabId curSid = current.getSlabId(); if (context->is_absolute(curSid)) continue;
            context->mark_absolute(curSid); callback(current);
            if (current->isListMode()) { current->forEachList([&](CPtr<ListreeValueRef>& ref) { if (ref && ref->getValue()) { CPtr<ListreeValue> child = ref->getValue(); if (!context->is_absolute(child.getSlabId())) enqueueTraversalValue(queue, child); } }, forward); } 
            else { current->forEachTree([&](const std::string&, CPtr<ListreeItem>& item) { if (item) item->forEachValue([&](CPtr<ListreeValue>& val) { if (val && !context->is_absolute(val.getSlabId())) enqueueTraversalValue(queue, val); }, forward); }, forward); }
        }
    } else {
        context->mark_absolute(sid); context->mark_recursive(sid); callback(startNode);
        TraversalOptions recOpts = options; recOpts.from = nullptr;
        if (startNode->isListMode()) { startNode->forEachList([&](CPtr<ListreeValueRef>& ref) { if (ref && ref->getValue()) ref->getValue()->traverse(callback, recOpts, context); }, forward); } 
        else { startNode->forEachTree([&](const std::string&, CPtr<ListreeItem>& item) { if (item) item->forEachValue([&](CPtr<ListreeValue>& val) { if (val) val->traverse(callback, recOpts, context); }, forward); }, forward); }
        context->unmark_recursive(sid);
    }
}

void ListreeValue::toDot(std::ostream& os, const std::string& label) const {
    os << "digraph \"" << label << "\" {\n  node [fontname=\"Helvetica\"];\n  edge [fontname=\"Helvetica\"];\n";
    TraversalContext visited;
    std::function<void(CPtr<ListreeValue>)> visitLtv = [&](CPtr<ListreeValue> ltv) {
        if (!ltv) return;
        SlabId sid = ltv.getSlabId();
        if (visited.is_absolute(sid)) return;
        visited.mark_absolute(sid);
        std::string id = "LTV" + std::to_string(sid.first) + "_" + std::to_string(sid.second);
        std::string lstr;
        if (ltv->isEmpty()) lstr = "null";
        else if ((ltv->getFlags() & LtvFlags::Binary) != LtvFlags::None) lstr = "binary[" + std::to_string(ltv->getLength()) + "]";
        else if (ltv->getData() && ltv->getLength() > 0) {
            lstr = std::string((const char*)ltv->getData(), ltv->getLength());
            size_t pos = 0; while ((pos = lstr.find('"', pos)) != std::string::npos) { lstr.replace(pos, 1, "\\\""); pos += 2; }
        }
        os << "  " << id << " [shape=box style=filled fillcolor=lightsteelblue label=\"" << lstr << "\"];\n";
        if (ltv->isListMode()) {
            ltv->forEachList([&](CPtr<ListreeValueRef>& ref) {
                if (ref) {
                    std::string refId = "LTVR" + std::to_string(ref.getSlabId().first) + "_" + std::to_string(ref.getSlabId().second);
                    os << "  " << refId << " [shape=point label=\"\"];\n  " << id << " -> " << refId << " [color=purple];\n";
                    CPtr<ListreeValue> child = ref->getValue(); if (child) { std::string childId = "LTV" + std::to_string(child.getSlabId().first) + "_" + std::to_string(child.getSlabId().second); os << "  " << refId << " -> " << childId << " [color=purple];\n"; visitLtv(child); }
                }
            });
        } else {
            ltv->forEachTree([&](const std::string& name, CPtr<ListreeItem>& item) {
                if (item) {
                    std::string itemId = "LTI" + std::to_string(item.getSlabId().first) + "_" + std::to_string(item.getSlabId().second);
                    os << "  " << itemId << " [shape=ellipse label=\"" << name << "\"];\n  " << id << " -> " << itemId << " [color=blue];\n";
                    item->forEachRef([&](CPtr<ListreeValueRef>& ref) {
                        if (ref) {
                            std::string refId = "LTVR" + std::to_string(ref.getSlabId().first) + "_" + std::to_string(ref.getSlabId().second);
                            os << "  " << refId << " [shape=point label=\"\"];\n  " << itemId << " -> " << refId << ";\n";
                            CPtr<ListreeValue> child = ref->getValue(); if (child) { std::string childId = "LTV" + std::to_string(child.getSlabId().first) + "_" + std::to_string(child.getSlabId().second); os << "  " << refId << " -> " << childId << ";\n"; visitLtv(child); }
                        }
                    });
                }
            });
        }
    };
    try { SlabId rootSid = Allocator<ListreeValue>::getAllocator().getSlabId(this); visitLtv(CPtr<ListreeValue>(rootSid)); } catch (...) {}
    os << "}\n";
}

bool ListreeValue::unwind(SlabId sid) { return true; }
std::ostream& operator<<(std::ostream& os, const ListreeValue& ltv) {
    os << "LTV("; if (ltv.isEmpty()) os << "null"; else if ((ltv.getFlags() & LtvFlags::Binary) != LtvFlags::None) os << "binary[" << ltv.getLength() << "]"; else if (ltv.getData() && ltv.getLength() > 0) os << '"' << std::string((char*)ltv.getData(), ltv.getLength()) << '"'; else os << "empty";
    os << ", " << (ltv.isListMode() ? "list" : "tree") << ")"; return os;
}

void resetTransientListreeValue(ListreeValue& value) {
    if (value.list) value.list.modrefs(-1);
    if (value.tree) value.tree.modrefs(-1);

    if ((value.flags & LtvFlags::Iterator) != LtvFlags::None && value.payload.ext.ptr) {
        delete static_cast<Cursor*>(value.payload.ext.ptr);
    } else if (value.payload.ext.ptr && ((value.flags & LtvFlags::Free) != LtvFlags::None) && ((value.flags & LtvFlags::Immediate) == LtvFlags::None)) {
        free(value.payload.ext.ptr);
    }

    value.list = nullptr;
    value.tree = nullptr;
    value.payload.ext.ptr = nullptr;
    value.payload.ext.length = 0;
    value.pinnedCount = 0;
    value.flags = LtvFlags::Null;
}

CPtr<ListreeValue> createNullValue() { CPtr<ListreeValue> res; { SlabId sid = Allocator<ListreeValue>::getAllocator().allocate(nullptr, 0, LtvFlags::Null); res = CPtr<ListreeValue>(sid); } return res; }
CPtr<ListreeValue> createListValue() { CPtr<ListreeValue> res; { SlabId sid = Allocator<ListreeValue>::getAllocator().allocate(nullptr, 0, LtvFlags::List); res = CPtr<ListreeValue>(sid); } return res; }
CPtr<ListreeValue> createStringValue(const std::string& str, LtvFlags flags) { CPtr<ListreeValue> res; { SlabId sid = Allocator<ListreeValue>::getAllocator().allocate(str, flags); res = CPtr<ListreeValue>(sid); } return res; }
CPtr<ListreeValue> createBinaryValue(const void* data, size_t length) { 
    CPtr<ListreeValue> res; 
    { SlabId sid = Allocator<ListreeValue>::getAllocator().allocate(const_cast<void*>(data), length, LtvFlags::Free | LtvFlags::Binary); res = CPtr<ListreeValue>(sid); } 
    return res; 
}

CPtr<ListreeValue> createCursorValue(Cursor* cursor) {
    CPtr<ListreeValue> res;
    // We pass the cursor pointer directly. The ListreeValue destructor will delete it.
    // We do NOT use LtvFlags::Duplicate, because we want to take ownership of the pointer.
    { SlabId sid = Allocator<ListreeValue>::getAllocator().allocate(cursor, sizeof(Cursor), LtvFlags::Iterator); res = CPtr<ListreeValue>(sid); }
    return res;
}

void addNamedItem(CPtr<ListreeValue>& ltv, const std::string& name, CPtr<ListreeValue> value) {
    if (!ltv || ltv->isListMode()) return;
    CPtr<ListreeItem> item = ltv->find(name, true);
    if (item) item->addValue(value, false);
}

void addListItem(CPtr<ListreeValue>& ltv, CPtr<ListreeValue> value) {
    if (!ltv || !ltv->isListMode()) return;
    ltv->put(value);
}

} // namespace agentc
