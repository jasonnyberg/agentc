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
#include <iostream>
#include <cstring>
#include "../core/alloc.h"
#include "../core/container.h"
#include "debug_helpers.h"

#include <atomic>
#include <memory>
#include <unordered_set>

template<typename T, typename Enable>
struct ArenaWatermarkResetTraits;

namespace agentc {

class ListreeValue;
class ListreeItem;
class ListreeValueRef;

// TraversalContext tracks visited SlabIds during listree traversal for cycle
// detection. Uses std::unordered_set<SlabId> to correctly handle all 16-bit
// slab indices and all SLAB_SIZE offsets (both can exceed 255, which the
// former manual bit-array with toIndex = (first<<8)|second could not).
class TraversalContext {
public:
    TraversalContext();
    ~TraversalContext();
    void mark_absolute(SlabId sid);
    bool is_absolute(SlabId sid) const;
    void mark_recursive(SlabId sid);
    void unmark_recursive(SlabId sid);
    bool is_recursive(SlabId sid) const;
    void clear();
private:
    std::unordered_set<SlabId> absolute_visited;
    std::unordered_set<SlabId> recursive_visited;
};

// LtvFlags encodes the storage class and interpretation of a ListreeValue's
// data pointer.  Flags can be combined; the most common combinations are
// described on each enumerator.
enum class LtvFlags {
    None       = 0x00000000,

    // Duplicate: the data buffer was malloc'd by this node (a copy of the
    // original string/bytes).  The destructor must free it.  This is the
    // default for string-valued nodes (createStringValue / the string
    // constructor).
    Duplicate  = 0x00000001,

    // Own: the node takes ownership of a caller-supplied raw pointer without
    // copying it.  The destructor must free it.  Combined with Duplicate
    // this forms the Free alias used for heap-owned data.
    Own        = 0x00000002,

    // Binary: data is opaque bytes rather than a NUL-terminated string.
    // Consumers use getLength() to know the byte count and must not treat
    // getData() as a string.  Used for int/double/pointer FFI values and
    // VM integer literals.
    Binary     = 0x00000008,

    // Null: the node has no payload (empty/absent value).  isEmpty() returns
    // true.  Default-constructed ListreeValues start with this flag set.
    Null       = 0x00000200,

    // Immediate: Small String Optimization (SSO). The string/binary payload is
    // stored directly inline within the 16 bytes reserved for data and dataLength.
    // Zero allocations required. Max length: 15 bytes.
    Immediate  = 0x00000400,

    // StaticView: indicates the node's data pointer is a non-owning view of a
    // static buffer (such as parsed Edict source code or memory-mapped API schema).
    // The data is not freed when the node is destroyed.
    StaticView = 0x00000800,

    // SlabBlob: indicates the node's data is an offset/length pair pointing to
    // a variable-length byte array stored in the slab-backed BlobAllocator.
    SlabBlob   = 0x00001000,

    // LogicVar: marks a ListreeValue as a miniKanren logic variable.  The
    // data holds a string representation of the variable id.  Consumers
    // (kanren.cpp::isLogicVar) test for this flag before treating the value
    // as a logic variable rather than a concrete term.
    LogicVar   = 0x00002000,

    // Iterator: data points to a live Cursor object.  The destructor calls
    // the cursor's destructor rather than free()ing the pointer.  Used by
    // createCursorValue() so that cursor-valued nodes can be stored in the
    // listree and automatically cleaned up.
    Iterator   = 0x00004000,

    // List: the node is in "list mode" — its primary container is the CLL
    // list rather than the AA-tree.  isListMode() tests this flag.
    List       = 0x00080000,

    // ReadOnly: the node (and, when set via setReadOnly(recursive=true), all
    // reachable descendants) is permanently immutable.  Mutation attempts via
    // find(insert=true), put(), remove(), and get(pop=true) are silently
    // refused.  Once set, this flag cannot be cleared.  Read access from
    // multiple threads is safe after the flag is set — no locks are needed
    // because no writer can run concurrently.  The flag is stripped during
    // arena persistence (restored nodes start mutable).
    ReadOnly   = 0x00200000,

    // Free: convenience alias for (Duplicate | Own).  A node carrying Free
    // owns its data buffer and the destructor must free it.  Used as a
    // quick test: "does this node own its memory?"
    Free       = Duplicate | Own,
};

inline LtvFlags operator|(LtvFlags a, LtvFlags b) { return static_cast<LtvFlags>(static_cast<int>(a) | static_cast<int>(b)); }
inline LtvFlags operator&(LtvFlags a, LtvFlags b) { return static_cast<LtvFlags>(static_cast<int>(a) & static_cast<int>(b)); }
inline LtvFlags operator~(LtvFlags a) { return static_cast<LtvFlags>(~static_cast<int>(a)); }
inline bool operator!=(LtvFlags a, LtvFlags b) { return static_cast<int>(a) != static_cast<int>(b); }

enum class TraversalOrder { DepthFirst, BreadthFirst };
enum class TraversalDirection { Forward, Backward };

struct TraversalOptions {
    CPtr<ListreeValue> from = nullptr;
    TraversalOrder order = TraversalOrder::DepthFirst;
    TraversalDirection direction = TraversalDirection::Forward;
};

class ListreeValueRef {
    template<typename, typename> friend struct ::ArenaPersistenceTraits;
    template<typename, typename> friend struct ::ArenaWatermarkResetTraits;
private:
    CPtr<ListreeValue> value;
public:
    ListreeValueRef();
    ListreeValueRef(::ArenaRestoreTag, SlabId valueSid) : value(CPtr<ListreeValue>::adoptRaw(valueSid)) {}
    explicit ListreeValueRef(CPtr<ListreeValue> value);
    ~ListreeValueRef();
    CPtr<ListreeValue> getValue() const { return value; }
    static bool unwind(SlabId sid);
    friend std::ostream& operator<<(std::ostream& os, const ListreeValueRef& ltvr);
};

class ListreeItem {
    friend class Cursor;
    template<typename, typename> friend struct ::ArenaPersistenceTraits;
    template<typename, typename> friend struct ::ArenaWatermarkResetTraits;
private:
    std::string name;
    CPtr<CLL<ListreeValueRef>> values;
public:
    ListreeItem();
    ListreeItem(::ArenaRestoreTag, std::string name, SlabId valuesSid)
        : name(std::move(name)), values(CPtr<CLL<ListreeValueRef>>::adoptRaw(valuesSid)) {}
    explicit ListreeItem(const std::string& name);
    ~ListreeItem();
    const std::string& getName() const { return name; }
    CPtr<ListreeValueRef> addValue(CPtr<ListreeValue>& value, bool atEnd = false);
    CPtr<ListreeValue> getValue(bool pop = false, bool fromEnd = false);
    void forEachValue(const std::function<void(CPtr<ListreeValue>&)>& callback, bool forward = true);
    void forEachRef(const std::function<void(CPtr<ListreeValueRef>&)>& callback, bool forward = true);
    static bool unwind(SlabId sid);
    friend std::ostream& operator<<(std::ostream& os, const ListreeItem& lti);
};

class ListreeValue {
    template<typename, typename> friend struct ::ArenaPersistenceTraits;
    template<typename, typename> friend struct ::ArenaWatermarkResetTraits;
    friend void resetTransientListreeValue(ListreeValue& value);
private:
    CPtr<CLL<ListreeValueRef>> list;
    CPtr<AATree<ListreeItem>> tree;
    LtvFlags flags;
    struct ExtPayload {
        void* ptr;
        size_t length;
    };
    struct SsoPayload {
        char bytes[15];
        uint8_t length;
    };
    union {
        ExtPayload ext;
        SsoPayload sso;
    } payload;
    std::atomic<int> pinnedCount;
    void initContainer();
public:
    ListreeValue();
    ListreeValue(::ArenaRestoreTag,
                 SlabId listSid,
                 SlabId treeSid,
                 LtvFlags flags,
                 void* restoredData,
                 size_t length,
                 int restoredPinnedCount)
        : list(CPtr<CLL<ListreeValueRef>>::adoptRaw(listSid)),
          tree(CPtr<AATree<ListreeItem>>::adoptRaw(treeSid)),
          flags(flags),
          pinnedCount(restoredPinnedCount) {
        // Route through the same storage logic as the normal constructor so that
        // SSO, BlobAllocator, and StaticView paths are all handled correctly.
        // exportSlot normalizes flags to Duplicate so wantsCopy will be true here.
        bool wantsCopy = ((flags & LtvFlags::Duplicate) != LtvFlags::None) || ((flags & LtvFlags::Free) != LtvFlags::None);
        if (length > 0 && length <= 15 && wantsCopy) {
            this->flags = static_cast<LtvFlags>(static_cast<int>(this->flags) & ~(static_cast<int>(LtvFlags::Duplicate) | static_cast<int>(LtvFlags::Free))) | LtvFlags::Immediate;
            if (restoredData) memcpy(this->payload.sso.bytes, restoredData, length); else memset(this->payload.sso.bytes, 0, length);
            this->payload.sso.length = static_cast<uint8_t>(length);
        } else if (length > 0 && wantsCopy) {
            SlabId blobId = BlobAllocator::getAllocator().allocate(restoredData, length);
            this->payload.ext.ptr = nullptr;
            memcpy(&this->payload.ext.ptr, &blobId, sizeof(SlabId));
            this->flags = static_cast<LtvFlags>(static_cast<int>(this->flags) & ~(static_cast<int>(LtvFlags::Duplicate) | static_cast<int>(LtvFlags::Free))) | LtvFlags::SlabBlob;
            this->payload.ext.length = length;
        } else {
            this->payload.ext.ptr = restoredData;
            this->payload.ext.length = length;
        }
    }
    ListreeValue(void* data, size_t length, LtvFlags flags);
    ListreeValue(const std::string& str, LtvFlags flags = LtvFlags::Duplicate);
    ~ListreeValue();
    void pin() { pinnedCount.fetch_add(1, std::memory_order_relaxed); }
    void unpin() { if (pinnedCount.load(std::memory_order_relaxed) > 0) pinnedCount.fetch_sub(1, std::memory_order_relaxed); }
    int getPinnedCount() const { return pinnedCount.load(std::memory_order_relaxed); }
    bool isReadOnly() const { return (flags & LtvFlags::ReadOnly) != LtvFlags::None; }
    // Mark this node (and optionally all descendants) permanently immutable.
    // After this call no writer may mutate the branch; the flag cannot be cleared.
    void setReadOnly(bool recursive = false);
    bool isListMode() const { return (flags & LtvFlags::List) != LtvFlags::None; }
    bool isEmpty() const { return (flags & LtvFlags::Null) != LtvFlags::None || getLength() == 0; }
    CPtr<ListreeItem> find(const std::string& name, bool insert = false);
    CPtr<ListreeItem> remove(const std::string& name);
    CPtr<ListreeValueRef> put(CPtr<ListreeValue> value, bool atEnd = true);
    CPtr<ListreeValue> get(bool pop = false, bool fromEnd = false);
    void* getData() const { 
        if ((flags & LtvFlags::Immediate) != LtvFlags::None) {
            return (void*)payload.sso.bytes;
        }
        if ((flags & LtvFlags::SlabBlob) != LtvFlags::None) {
            SlabId id;
            std::memcpy(static_cast<void*>(&id), &payload.ext.ptr, sizeof(SlabId));
            return BlobAllocator::getAllocator().getPointer(id);
        }
        return payload.ext.ptr; 
    }
    size_t getLength() const { 
        if ((flags & LtvFlags::Immediate) != LtvFlags::None) {
            return payload.sso.length;
        }
        return payload.ext.length; 
    }
    LtvFlags getFlags() const { return flags; }
    void setFlags(LtvFlags f) { flags = flags | f; }
    // ReadOnly is a one-way flag; it cannot be cleared once set.
    void clearFlags(LtvFlags f) {
        f = f & ~LtvFlags::ReadOnly;
        flags = static_cast<LtvFlags>(static_cast<int>(flags) & ~static_cast<int>(f));
    }
    CPtr<ListreeValue> duplicate() const;
    CPtr<ListreeValue> copy(int maxDepth = -1, void* ctx = nullptr) const;
    void forEachList(const std::function<void(CPtr<ListreeValueRef>&)>& callback, bool forward = true);
    void forEachTree(const std::function<void(const std::string&, CPtr<ListreeItem>&)>& callback, bool forward = true);
    void traverse(const std::function<void(CPtr<ListreeValue>)>& callback, TraversalOptions options = {}, std::shared_ptr<TraversalContext> context = nullptr);
    void toDot(std::ostream& os, const std::string& label = "Listree") const;
    static bool unwind(SlabId sid);
    friend std::ostream& operator<<(std::ostream& os, const ListreeValue& ltv);
};

CPtr<ListreeValue> createNullValue();
CPtr<ListreeValue> createListValue();
CPtr<ListreeValue> createStringValue(const std::string& str, LtvFlags flags = LtvFlags::Duplicate);
CPtr<ListreeValue> createBinaryValue(const void* data, size_t length);
class Cursor;
CPtr<ListreeValue> createCursorValue(Cursor* cursor);
void addNamedItem(CPtr<ListreeValue>& ltv, const std::string& name, CPtr<ListreeValue> value);
void addListItem(CPtr<ListreeValue>& ltv, CPtr<ListreeValue> value);

/// Serialize a Listree value to a JSON string.
/// - Tree nodes become JSON objects.
/// - List nodes become JSON arrays.
/// - String/scalar leaves become JSON strings (properly escaped).
/// - Null/empty nodes become JSON null.
/// - Binary nodes (FFI pointers, bytecode) are rendered as JSON null.
/// Cycles are detected and rendered as null rather than looping.
std::string toJson(CPtr<ListreeValue> value);

/// Parse a JSON string into a Listree value.
/// - JSON objects become tree-mode dict nodes.
/// - JSON arrays become list-mode nodes.
/// - JSON strings become string leaf nodes (escape sequences decoded).
/// - JSON null becomes a null Listree node (LtvFlags::Null).
/// - JSON true/false become the string leaves "true"/"false".
/// - JSON numbers become string leaves preserving the raw text (e.g. "42").
/// Returns nullptr if the input is not valid JSON.
/// Note: fromJson(toJson(v)) == v for all Listree values that contain only
/// strings, nulls, dicts, and lists (binary nodes and stacked values are
/// lossy through to_json and cannot be recovered).
CPtr<ListreeValue> fromJson(const std::string& json);
void resetTransientListreeValue(ListreeValue& value);

} // namespace agentc

template<>
struct ArenaPersistenceTraits<agentc::ListreeValueRef, void> {
    static constexpr bool supported = true;
    static constexpr ArenaSlabEncoding encoding = ArenaSlabEncoding::Structured;

    static bool exportSlot(const agentc::ListreeValueRef& ref, std::string& payload) {
        payload.clear();
        arena_persistence_detail::appendSlabId(payload, ref.value.getSlabId());
        return true;
    }

    static bool restoreSlot(agentc::ListreeValueRef* target, const std::string& payload) {
        size_t cursor = 0;
        SlabId valueSid;
        if (!arena_persistence_detail::readSlabId(payload, cursor, valueSid) || cursor != payload.size()) {
            return false;
        }
        new (target) agentc::ListreeValueRef(kArenaRestoreTag, valueSid);
        return true;
    }
};

template<>
struct ArenaWatermarkResetTraits<agentc::ListreeValueRef, void> {
    static constexpr bool strictEligible = true;

    static void resetTransient(agentc::ListreeValueRef& ref) {
        if (ref.value) ref.value.modrefs(-1);
    }
};

template<>
struct ArenaWatermarkResetTraits<agentc::ListreeItem, void> {
    static constexpr bool strictEligible = true;

    static void resetTransient(agentc::ListreeItem& item) {
        if (item.values) item.values.modrefs(-1);
        item.name.clear();
    }
};

template<>
struct ArenaPersistenceTraits<agentc::ListreeItem, void> {
    static constexpr bool supported = true;
    static constexpr ArenaSlabEncoding encoding = ArenaSlabEncoding::Structured;

    static bool exportSlot(const agentc::ListreeItem& item, std::string& payload) {
        payload.clear();
        arena_persistence_detail::appendString(payload, item.name);
        arena_persistence_detail::appendSlabId(payload, item.values.getSlabId());
        return true;
    }

    static bool restoreSlot(agentc::ListreeItem* target, const std::string& payload) {
        size_t cursor = 0;
        std::string name;
        SlabId valuesSid;
        if (!arena_persistence_detail::readString(payload, cursor, name) ||
            !arena_persistence_detail::readSlabId(payload, cursor, valuesSid) ||
            cursor != payload.size()) {
            return false;
        }
        new (target) agentc::ListreeItem(kArenaRestoreTag, std::move(name), valuesSid);
        return true;
    }
};

template<>
struct ArenaPersistenceTraits<agentc::ListreeValue, void> {
    static constexpr bool supported = true;
    static constexpr ArenaSlabEncoding encoding = ArenaSlabEncoding::Structured;

    static bool exportSlot(const agentc::ListreeValue& value, std::string& payload) {
        payload.clear();
        if ((value.flags & agentc::LtvFlags::Iterator) != agentc::LtvFlags::None) {
            return false;
        }

        const bool hasOwnedData = value.getData() && value.getLength() > 0 &&
            (((value.flags & agentc::LtvFlags::Free) != agentc::LtvFlags::None) || 
             ((value.flags & agentc::LtvFlags::Duplicate) != agentc::LtvFlags::None) ||
             ((value.flags & agentc::LtvFlags::Immediate) != agentc::LtvFlags::None) ||
             ((value.flags & agentc::LtvFlags::SlabBlob) != agentc::LtvFlags::None));
        if (value.getData() && value.getLength() > 0 && !hasOwnedData) {
            return false;
        }

        arena_persistence_detail::appendSlabId(payload, value.list.getSlabId());
        arena_persistence_detail::appendSlabId(payload, value.tree.getSlabId());
        // Normalize flags for serialization: strip transient storage flags; the restore
        // path will re-route through the normal constructor (SSO/BlobAllocator) using Duplicate.
        // ReadOnly is also stripped — restored nodes start mutable; the caller re-freezes if needed.
        auto exportFlags = static_cast<uint32_t>(value.flags) &
            ~(static_cast<uint32_t>(agentc::LtvFlags::SlabBlob) |
              static_cast<uint32_t>(agentc::LtvFlags::Immediate) |
              static_cast<uint32_t>(agentc::LtvFlags::Free) |
              static_cast<uint32_t>(agentc::LtvFlags::ReadOnly));
        exportFlags |= static_cast<uint32_t>(agentc::LtvFlags::Duplicate);
        arena_persistence_detail::appendPod(payload, exportFlags);
        size_t dlen = value.getLength();
        arena_persistence_detail::appendPod(payload, dlen);
                                                                        // pinnedCount is std::atomic<int>; extract the raw value before serializing.
        int pc = value.pinnedCount.load(std::memory_order_relaxed);
        arena_persistence_detail::appendPod(payload, pc);
        const void* data_ptr = value.getData();
        if (dlen > 0 && !data_ptr) {
            // dlen > 0 but null data pointer: write zeroes to avoid UB in appendBytes.
            std::string zeros(dlen, '\0');
            arena_persistence_detail::appendBytes(payload, zeros.data(), dlen);
        } else {
            // Normal case: always call appendBytes so restoreSlot's readBytes finds its size prefix.
            arena_persistence_detail::appendBytes(payload, data_ptr, dlen);
        }
        return true;
    }

    static bool restoreSlot(agentc::ListreeValue* target, const std::string& payload) {
        size_t cursor = 0;
        SlabId listSid;
        SlabId treeSid;
        uint32_t rawFlags = 0;
        size_t dataLength = 0;
        int pinnedCount = 0;
        std::vector<std::byte> bytes;
        if (!arena_persistence_detail::readSlabId(payload, cursor, listSid) ||
            !arena_persistence_detail::readSlabId(payload, cursor, treeSid) ||
            !arena_persistence_detail::readPod(payload, cursor, rawFlags) ||
            !arena_persistence_detail::readPod(payload, cursor, dataLength) ||
            !arena_persistence_detail::readPod(payload, cursor, pinnedCount) ||
            !arena_persistence_detail::readBytes(payload, cursor, bytes) ||
            cursor != payload.size() || bytes.size() != dataLength) {
            return false;
        }

        // Construct directly — the Duplicate flag routes the data through SSO or BlobAllocator.
        new (target) agentc::ListreeValue(kArenaRestoreTag,
                                      listSid,
                                      treeSid,
                                      static_cast<agentc::LtvFlags>(rawFlags),
                                      dataLength > 0 ? const_cast<void*>(static_cast<const void*>(bytes.data())) : nullptr,
                                      dataLength,
                                      pinnedCount);
        return true;
    }
};

template<>
struct ArenaWatermarkResetTraits<agentc::ListreeValue, void> {
    static constexpr bool strictEligible = true;

    static void resetTransient(agentc::ListreeValue& value) {
        agentc::resetTransientListreeValue(value);
    }
};
