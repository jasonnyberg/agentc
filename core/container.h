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

template<typename T, typename Enable>
struct ArenaWatermarkResetTraits;

namespace agentc { class Cursor; }

template<typename T>
class CLL {
    template<typename U> friend class Allocator;
    template<typename U> friend class CPtr;
    template<typename, typename> friend struct ArenaPersistenceTraits;
    template<typename, typename> friend struct ArenaWatermarkResetTraits;
    friend class agentc::Cursor;
private:
    CPtr<CLL<T>> lnk[2];
    CLL() : lnk{nullptr, nullptr}, data(nullptr) { lnk[0] = this; lnk[1] = this; }
    CLL(ArenaRestoreTag, SlabId prev, SlabId next, SlabId dataSid)
        : lnk{CPtr<CLL<T>>::adoptRaw(prev), CPtr<CLL<T>>::adoptRaw(next)}, data(CPtr<T>::adoptRaw(dataSid)) {}
    template<typename... Args>
    CLL(Args &&...args) : lnk{nullptr, nullptr} {
        lnk[0] = this; lnk[1] = this;
        if constexpr(sizeof...(Args) > 0) data = CPtr<T>(std::forward<Args>(args)...);
    }
public:
    CPtr<T> data;
    ~CLL() {}
    static bool unwind(SlabId sid) { return true; }
    CLL<T> &store(CPtr<CLL<T>> &b, bool fwd = true) { b.modrefs(1); return splice(b, fwd); }
    CLL<T> &splice(CPtr<CLL<T>> &b, bool fwd = true) {
        if (!b || !lnk[fwd] || !lnk[!fwd] || !b->lnk[fwd] || !b->lnk[!fwd]) return *this;
        CPtr<CLL<T>> bPrev = b->lnk[!fwd]; CPtr<CLL<T>> thisNext = lnk[fwd];
        if (bPrev && thisNext) { bPrev->lnk[fwd] = thisNext; thisNext->lnk[!fwd] = bPrev; }
        lnk[fwd] = b; b->lnk[!fwd] = this; return *this;
    }
    CLL<T> &put(CPtr<CLL<T>> &b, bool fwd = true) { return store(b, fwd); }
    CLL<T> &get(bool fwd = true) { return *lnk[fwd]; }
    CLL<T> &remove(bool fwd = true) {
        CPtr<CLL<T>> node = lnk[fwd]; lnk[fwd] = node->lnk[fwd]; lnk[fwd]->lnk[!fwd] = this;
        node->lnk[0] = node; node->lnk[1] = node; node.modrefs(-1); return *this;
    }
    CLL<T> &pop(bool fwd = true) { CLL<T> &node = *lnk[fwd]; remove(fwd); return node; }
    void forEach(std::function<void(CPtr<T>&)> callback, bool forward = true) {
        CPtr<CLL<T>> start = this; CPtr<CLL<T>> cur = this; int dir = forward ? 1 : 0;
        do { if (cur->data) callback(cur->data); cur = cur->lnk[dir]; } while (cur != start && cur);
    }
};

template<typename T>
class AATree {
    template<typename U> friend class Allocator;
    template<typename U> friend class CPtr;
    template<typename, typename> friend struct ArenaPersistenceTraits;
    template<typename, typename> friend struct ArenaWatermarkResetTraits;
private:
    CPtr<AATree<T>> lnk[2];
    AATree() : lnk{nullptr, nullptr}, level(1) {}
    AATree(ArenaRestoreTag, std::string nodeName, unsigned nodeLevel, SlabId left, SlabId right, SlabId dataSid)
        : lnk{CPtr<AATree<T>>::adoptRaw(left), CPtr<AATree<T>>::adoptRaw(right)}, name(std::move(nodeName)), level(nodeLevel), data(CPtr<T>::adoptRaw(dataSid)) {}
    template<typename... Args>
    AATree(Args &&...args) : lnk{nullptr, nullptr}, level(1) {
        if constexpr(sizeof...(Args) > 0) data = CPtr<T>(std::forward<Args>(args)...);
    }
public:
    std::string name; unsigned level; CPtr<T> data;
    ~AATree() {}
    static bool unwind(SlabId sid) { return true; }
    AATree<T> &add(const std::string &nodeName, const CPtr<T> &value) {
        if (name.empty() && !data) { name = nodeName; data = value; return *this; }
        CPtr<AATree<T>> cur = this; CPtr<AATree<T>> parent = nullptr; bool dir = false;
        while (cur) {
            if (nodeName < cur->name) { parent = cur; cur = cur->lnk[0]; dir = 0; }
            else if (nodeName > cur->name) { parent = cur; cur = cur->lnk[1]; dir = 1; }
            else { if (cur->data) cur->data.modrefs(-1); cur->data = value; return *this; }
        }
        SlabId nSid = Allocator<AATree<T>>::getAllocator().allocate();
        new (Allocator<AATree<T>>::getAllocator().getPtr(nSid)) AATree<T>();
        CPtr<AATree<T>> node(nSid); node->name = nodeName; node->data = value;
        if (parent) {
            parent->lnk[dir] = node;
        }
        return *this;
    }
    CPtr<AATree<T>> find(const std::string &nodeName) {
        CPtr<AATree<T>> cur = this;
        while (cur) { if (nodeName < cur->name) cur = cur->lnk[0]; else if (nodeName > cur->name) cur = cur->lnk[1]; else return cur; }
        return nullptr;
    }
    
    // Recursive remove helper
    CPtr<AATree<T>> removeRecursive(CPtr<AATree<T>> node, const std::string &nodeName) {
        if (!node) return nullptr;
        if (nodeName < node->name) {
            node->lnk[0] = removeRecursive(node->lnk[0], nodeName);
        } else if (nodeName > node->name) {
            node->lnk[1] = removeRecursive(node->lnk[1], nodeName);
        } else {
            // Node found
            if (!node->lnk[0] && !node->lnk[1]) {
                return nullptr; // Leaf
            } else if (!node->lnk[0]) {
                return node->lnk[1]; // One child (right)
            } else if (!node->lnk[1]) {
                return node->lnk[0]; // One child (left)
            } else {
                // Two children: find successor (min of right)
                CPtr<AATree<T>> succ = node->lnk[1];
                while (succ->lnk[0]) succ = succ->lnk[0];
                node->name = succ->name;
                node->data = succ->data;
                node->lnk[1] = removeRecursive(node->lnk[1], succ->name);
            }
        }
        return node;
    }

    CPtr<AATree<T>> remove(const std::string &nodeName) { 
        // Use removeRecursive to get the new root of the subtree
        // We must construct a CPtr for 'this' to pass to recursive function?
        // Actually, removeRecursive takes CPtr.
        SlabId thisSid = Allocator<AATree<T>>::getAllocator().getSlabId(this);
        CPtr<AATree<T>> thisPtr(thisSid);
        
        return removeRecursive(thisPtr, nodeName);
    } 
    
    void forEach(std::function<void(const std::string&, CPtr<T>&)> callback, bool forward = true) {
        int first = forward ? 0 : 1; int second = forward ? 1 : 0;
        if (lnk[first]) lnk[first]->forEach(callback, forward);
        if (!name.empty() || data) callback(name, data);
        if (lnk[second]) lnk[second]->forEach(callback, forward);
    }
};

template<typename T>
struct ArenaPersistenceTraits<CLL<T>, void> {
    static constexpr bool supported = true;
    static constexpr ArenaSlabEncoding encoding = ArenaSlabEncoding::Structured;

    static bool exportSlot(const CLL<T>& node, std::string& payload) {
        payload.clear();
        arena_persistence_detail::appendSlabId(payload, node.lnk[0].getSlabId());
        arena_persistence_detail::appendSlabId(payload, node.lnk[1].getSlabId());
        arena_persistence_detail::appendSlabId(payload, node.data.getSlabId());
        return true;
    }

    static bool restoreSlot(CLL<T>* target, const std::string& payload) {
        size_t cursor = 0;
        SlabId prev;
        SlabId next;
        SlabId dataSid;
        if (!arena_persistence_detail::readSlabId(payload, cursor, prev) ||
            !arena_persistence_detail::readSlabId(payload, cursor, next) ||
            !arena_persistence_detail::readSlabId(payload, cursor, dataSid) ||
            cursor != payload.size()) {
            return false;
        }
        new (target) CLL<T>(kArenaRestoreTag, prev, next, dataSid);
        return true;
    }
};

template<typename T>
struct ArenaWatermarkResetTraits<CLL<T>, void> {
    static constexpr bool strictEligible = true;

    static void resetTransient(CLL<T>& node) {
        if (node.lnk[0]) node.lnk[0].modrefs(-1);
        if (node.lnk[1]) node.lnk[1].modrefs(-1);
        if (node.data) node.data.modrefs(-1);
    }
};

template<typename T>
struct ArenaWatermarkResetTraits<AATree<T>, void> {
    static constexpr bool strictEligible = true;

    static void resetTransient(AATree<T>& node) {
        if (node.lnk[0]) node.lnk[0].modrefs(-1);
        if (node.lnk[1]) node.lnk[1].modrefs(-1);
        if (node.data) node.data.modrefs(-1);
        node.name.clear();
        node.level = 0;
    }
};

template<typename T>
struct ArenaPersistenceTraits<AATree<T>, void> {
    static constexpr bool supported = true;
    static constexpr ArenaSlabEncoding encoding = ArenaSlabEncoding::Structured;

    static bool exportSlot(const AATree<T>& node, std::string& payload) {
        payload.clear();
        arena_persistence_detail::appendString(payload, node.name);
        arena_persistence_detail::appendPod(payload, node.level);
        arena_persistence_detail::appendSlabId(payload, node.lnk[0].getSlabId());
        arena_persistence_detail::appendSlabId(payload, node.lnk[1].getSlabId());
        arena_persistence_detail::appendSlabId(payload, node.data.getSlabId());
        return true;
    }

    static bool restoreSlot(AATree<T>* target, const std::string& payload) {
        size_t cursor = 0;
        std::string name;
        unsigned level = 0;
        SlabId left;
        SlabId right;
        SlabId dataSid;
        if (!arena_persistence_detail::readString(payload, cursor, name) ||
            !arena_persistence_detail::readPod(payload, cursor, level) ||
            !arena_persistence_detail::readSlabId(payload, cursor, left) ||
            !arena_persistence_detail::readSlabId(payload, cursor, right) ||
            !arena_persistence_detail::readSlabId(payload, cursor, dataSid) ||
            cursor != payload.size()) {
            return false;
        }
        new (target) AATree<T>(kArenaRestoreTag, std::move(name), level, left, right, dataSid);
        return true;
    }
};
