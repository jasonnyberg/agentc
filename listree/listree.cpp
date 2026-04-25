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
#include <unordered_set>
#include <cstdio>

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

void ListreeValue::setReadOnly(bool recursive) {
    // Never freeze bytecode/thunk nodes.  The VM stores the instruction pointer
    // directly inside these nodes via find(".ip", insert=true) (writeFrameIp).
    // Freezing them would refuse that write and break all compiled thunk dispatch.
    if ((flags & LtvFlags::Binary) != LtvFlags::None) return;
    flags = flags | LtvFlags::ReadOnly;
    if (!recursive) return;
    // Walk all reachable ListreeValue descendants breadth-first and mark each
    // read-only, skipping Binary (thunk/bytecode) nodes for the same reason.
    try {
        SlabId sid = Allocator<ListreeValue>::getAllocator().getSlabId(this);
        CPtr<ListreeValue> self(sid);
        TraversalOptions opts;
        opts.from = self;
        opts.order = TraversalOrder::BreadthFirst;
        traverse([](CPtr<ListreeValue> node) {
            if (node && (node->getFlags() & LtvFlags::Binary) == LtvFlags::None)
                node->flags = node->flags | LtvFlags::ReadOnly;
        }, opts, std::make_shared<TraversalContext>());
    } catch (...) {
        // If getSlabId fails the flag on this node is already set above;
        // child marking is best-effort.
    }
}

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
    } else {
        // Empty string: use SSO with length 0. Do NOT set LtvFlags::Null —
        // an empty string is a distinct value from null.
        this->flags = static_cast<LtvFlags>(
            (static_cast<int>(this->flags) & ~(static_cast<int>(LtvFlags::Duplicate) | static_cast<int>(LtvFlags::Free)))
            | static_cast<int>(LtvFlags::Immediate));
        this->payload.sso.bytes[0] = 0;
        this->payload.sso.length = 0;
    }
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
    // Read-only nodes refuse structural insertions.
    if (insert && isReadOnly()) {
        LISTREE_DEBUG_WARNING() << "find: write refused on read-only ListreeValue";
        return nullptr;
    }
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
    if (isReadOnly()) {
        LISTREE_DEBUG_WARNING() << "remove: write refused on read-only ListreeValue";
        return nullptr;
    }
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
    if (isReadOnly()) {
        LISTREE_DEBUG_WARNING() << "put: write refused on read-only ListreeValue";
        return nullptr;
    }
    CPtr<ListreeValueRef> ref;
    { SlabId refSid = Allocator<ListreeValueRef>::getAllocator().allocate(value); ref = CPtr<ListreeValueRef>(refSid); }
    CPtr<CLL<ListreeValueRef>> node;
    { SlabId nodeSid = Allocator<CLL<ListreeValueRef>>::getAllocator().allocate(); node = CPtr<CLL<ListreeValueRef>>(nodeSid); node->data = ref; }
    list->store(node, atEnd);
    
    return ref;
}
CPtr<ListreeValue> ListreeValue::get(bool pop, bool fromEnd) {
    if (!isListMode() || !list) return nullptr;
    if (pop && isReadOnly()) {
        // Degrade to a non-destructive peek rather than corrupting a shared branch.
        LISTREE_DEBUG_WARNING() << "get(pop=true): degraded to peek on read-only ListreeValue";
        pop = false;
    }
    CPtr<CLL<ListreeValueRef>> current = &list->get(fromEnd);
    if (!current || !current->data) return nullptr;
    CPtr<ListreeValue> value = current->data->getValue();
    if (pop) list->remove(fromEnd);
    return value;
}
CPtr<ListreeValue> ListreeValue::duplicate() const { return copy(-1); }
CPtr<ListreeValue> ListreeValue::copy(int maxDepth) const {
    // Short-circuit: a read-only node is permanently immutable and safe to
    // share between VMs/threads.  Return a CPtr retain (O(1)) rather than
    // allocating a deep copy of the subtree.
    if (isReadOnly()) {
        try {
            SlabId sid = Allocator<ListreeValue>::getAllocator().getSlabId(this);
            return CPtr<ListreeValue>(sid);
        } catch (...) {
            // Slab ID lookup failed (e.g. node not slab-resident); fall through
            // to normal copy.
        }
    }
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
    value.pinnedCount.store(0, std::memory_order_relaxed);
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
    if (ltv->isReadOnly()) {
        LISTREE_DEBUG_WARNING() << "addNamedItem: write refused on read-only ListreeValue";
        return;
    }
    CPtr<ListreeItem> item = ltv->find(name, true);
    if (item) item->addValue(value, false);
}

void addListItem(CPtr<ListreeValue>& ltv, CPtr<ListreeValue> value) {
    if (!ltv || !ltv->isListMode()) return;
    if (ltv->isReadOnly()) {
        LISTREE_DEBUG_WARNING() << "addListItem: write refused on read-only ListreeValue";
        return;
    }
    ltv->put(value);
}

// ---------------------------------------------------------------------------
// JSON serialization
// ---------------------------------------------------------------------------

namespace {

void jsonEscapeString(const char* data, size_t len, std::string& out) {
    out += '"';
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void ltvToJsonHelper(CPtr<ListreeValue> v, std::string& out,
                     std::unordered_set<uint64_t>& visited, int depth) {
    // Null pointer or depth guard
    if (!v || depth > 256) { out += "null"; return; }

    // Cycle detection via slab identity
    SlabId sid = v.getSlabId();
    uint64_t key = (static_cast<uint64_t>(sid.first) << 16) | sid.second;
    if (!visited.insert(key).second) {
        // Already on the current recursion path — cycle; emit null
        out += "null";
        return;
    }

    const LtvFlags flags = v->getFlags();

    // Binary nodes are skipped (FFI pointers, bytecode, etc.)
    if ((flags & LtvFlags::Binary) != LtvFlags::None) {
        visited.erase(key);
        out += "null";
        return;
    }

    // List mode → JSON array (iterate in insertion order via backward links)
    if (v->isListMode()) {
        out += '[';
        bool first = true;
        v->forEachList([&](CPtr<ListreeValueRef>& ref) {
            if (!ref) return;
            CPtr<ListreeValue> child = ref->getValue();
            if (!child) return;
            if (!first) out += ',';
            first = false;
            ltvToJsonHelper(child, out, visited, depth + 1);
        }, false);  // false = backward direction = insertion order
        out += ']';
        visited.erase(key);
        return;
    }

    // Tree mode with named children → JSON object
    bool hasChildren = false;
    v->forEachTree([&](const std::string&, CPtr<ListreeItem>& item) {
        if (item) hasChildren = true;
    });

    if (hasChildren) {
        out += '{';
        bool first = true;
        v->forEachTree([&](const std::string& name, CPtr<ListreeItem>& item) {
            if (!item) return;
            CPtr<ListreeValue> child = item->getValue(false, false);
            if (!child) return;
            if (!first) out += ',';
            first = false;
            jsonEscapeString(name.data(), name.size(), out);
            out += ':';
            ltvToJsonHelper(child, out, visited, depth + 1);
        });
        out += '}';
        visited.erase(key);
        return;
    }

    // Leaf: explicit null flag → JSON null
    if ((flags & LtvFlags::Null) != LtvFlags::None) {
        out += "null";
        visited.erase(key);
        return;
    }

    // Leaf: string data.
    const char* data = static_cast<const char*>(v->getData());
    size_t len = v->getLength();
    jsonEscapeString(data ? data : "", len, out);
    visited.erase(key);
}

} // anonymous namespace

std::string toJson(CPtr<ListreeValue> value) {
    if (!value) return "null";
    std::string out;
    std::unordered_set<uint64_t> visited;
    ltvToJsonHelper(value, out, visited, 0);
    return out;
}

// ---------------------------------------------------------------------------
// JSON parser  (runtime from_json)
// ---------------------------------------------------------------------------

namespace {

struct JsonParser {
    const char* pos;
    const char* end;

    explicit JsonParser(const std::string& s)
        : pos(s.data()), end(s.data() + s.size()) {}

    void skip_ws() {
        while (pos < end && static_cast<unsigned char>(*pos) <= ' ') ++pos;
    }
    bool at_end() const { return pos >= end; }

    static int hex_val(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    void encode_utf8(uint32_t cp, std::string& out) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    // Read a JSON string from pos (which must point at the opening \")
    // and decode escape sequences into out.  Returns false on malformed input.
    bool parse_raw_string(std::string& out) {
        if (at_end() || *pos != '"') return false;
        ++pos;
        out.clear();
        while (pos < end && *pos != '"') {
            if (*pos != '\\') { out += *pos++; continue; }
            ++pos;  // consume backslash
            if (at_end()) return false;
            switch (*pos) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'u': {
                    if (end - pos < 5) return false;
                    int h0=hex_val(pos[1]),h1=hex_val(pos[2]),
                        h2=hex_val(pos[3]),h3=hex_val(pos[4]);
                    if (h0<0||h1<0||h2<0||h3<0) return false;
                    uint32_t cp = (uint32_t(h0)<<12)|(uint32_t(h1)<<8)|
                                  (uint32_t(h2)<<4)|uint32_t(h3);
                    pos += 4;
                    // Handle UTF-16 surrogate pairs
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                            end - pos >= 7 && pos[1]=='\\' && pos[2]=='u') {
                        int l0=hex_val(pos[3]),l1=hex_val(pos[4]),
                            l2=hex_val(pos[5]),l3=hex_val(pos[6]);
                        if (l0>=0&&l1>=0&&l2>=0&&l3>=0) {
                            uint32_t lo = (uint32_t(l0)<<12)|(uint32_t(l1)<<8)|
                                          (uint32_t(l2)<<4)|uint32_t(l3);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp-0xD800)<<10) + (lo-0xDC00);
                                pos += 6;
                            }
                        }
                    }
                    encode_utf8(cp, out);
                    break;
                }
                default: return false;
            }
            ++pos;
        }
        if (at_end() || *pos != '"') return false;
        ++pos;  // consume closing quote
        return true;
    }

    // Parse any JSON value; returns nullptr on error.
    CPtr<ListreeValue> parse_value() {
        skip_ws();
        if (at_end()) return nullptr;
        switch (*pos) {
            case '"': {
                std::string s;
                if (!parse_raw_string(s)) return nullptr;
                // createStringValue works correctly for empty strings
                return createStringValue(s);
            }
            case '{': return parse_object();
            case '[': return parse_array();
            case 'n':
                if (end-pos>=4 && std::memcmp(pos,"null",4)==0) { pos+=4; return createNullValue(); }
                return nullptr;
            case 't':
                if (end-pos>=4 && std::memcmp(pos,"true",4)==0) { pos+=4; return createStringValue("true"); }
                return nullptr;
            case 'f':
                if (end-pos>=5 && std::memcmp(pos,"false",5)==0) { pos+=5; return createStringValue("false"); }
                return nullptr;
            default:
                if (*pos=='-' || (*pos>='0' && *pos<='9')) return parse_number();
                return nullptr;
        }
    }

    CPtr<ListreeValue> parse_object() {
        if (at_end() || *pos != '{') return nullptr;
        ++pos;
        auto obj = createNullValue();
        skip_ws();
        if (!at_end() && *pos == '}') { ++pos; return obj; }  // empty object
        while (!at_end()) {
            skip_ws();
            if (at_end() || *pos != '"') return nullptr;
            std::string key;
            if (!parse_raw_string(key)) return nullptr;
            skip_ws();
            if (at_end() || *pos != ':') return nullptr;
            ++pos;
            auto val = parse_value();
            if (!val) return nullptr;
            addNamedItem(obj, key, val);
            skip_ws();
            if (at_end()) return nullptr;
            if (*pos == ',') { ++pos; continue; }
            if (*pos == '}') { ++pos; return obj; }
            return nullptr;
        }
        return nullptr;  // unterminated
    }

    CPtr<ListreeValue> parse_array() {
        if (at_end() || *pos != '[') return nullptr;
        ++pos;
        auto arr = createListValue();
        skip_ws();
        if (!at_end() && *pos == ']') { ++pos; return arr; }  // empty array
        while (!at_end()) {
            auto val = parse_value();
            if (!val) return nullptr;
            addListItem(arr, val);
            skip_ws();
            if (at_end()) return nullptr;
            if (*pos == ',') { ++pos; continue; }
            if (*pos == ']') { ++pos; return arr; }
            return nullptr;
        }
        return nullptr;  // unterminated
    }

    CPtr<ListreeValue> parse_number() {
        const char* start = pos;
        if (pos < end && *pos == '-') ++pos;
        while (pos < end && *pos >= '0' && *pos <= '9') ++pos;
        if (pos < end && *pos == '.') {
            ++pos;
            while (pos < end && *pos >= '0' && *pos <= '9') ++pos;
        }
        if (pos < end && (*pos == 'e' || *pos == 'E')) {
            ++pos;
            if (pos < end && (*pos == '+' || *pos == '-')) ++pos;
            while (pos < end && *pos >= '0' && *pos <= '9') ++pos;
        }
        if (pos == start) return nullptr;
        return createStringValue(std::string(start, pos - start));
    }
};

} // anonymous namespace

CPtr<ListreeValue> fromJson(const std::string& json) {
    if (json.empty()) return nullptr;
    JsonParser p(json);
    auto result = p.parse_value();
    p.skip_ws();
    // Require all input consumed (no trailing garbage)
    return (result && p.at_end()) ? result : nullptr;
}

} // namespace agentc
