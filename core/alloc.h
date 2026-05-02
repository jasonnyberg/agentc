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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <memory>
#include <new>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <bitset>
#include <iostream>
#include <mutex>
#include <type_traits>
#include <filesystem>
#include <sys/mman.h>
#include <unistd.h>
#include "debug.h"

#define SLAB_SIZE (1<<10)
#define NUM_SLABS (1<<12)

class SlabId : public std::pair<uint16_t, uint16_t> {
public:
    SlabId(uint16_t index=0, uint16_t offset=0) : std::pair<uint16_t, uint16_t>(index, offset) {}
    SlabId(const std::pair<uint16_t, uint16_t> &other) : std::pair<uint16_t, uint16_t>(other) {}
    friend std::ostream &operator<<(std::ostream &os, const SlabId &si) {
        os << "@[" << si.first << ":" << si.second << "]";
        return os;
    }
    operator bool() const { return first != 0 || second != 0; }
};

namespace std {
    template <>
    struct hash<SlabId> {
        size_t operator()(const SlabId& si) const {
            return (static_cast<size_t>(si.first) << 16) | si.second;
        }
    };
}

struct ArenaCheckpointMetadata {
    uint32_t version = 1;
    uint32_t slabSize = SLAB_SIZE;
    uint32_t activeSlabCount = 0;
    uint32_t liveSlotCount = 0;
    uint64_t allocationLogSize = 0;
    uint16_t highestSlabIndex = 0;
    uint16_t highestSlabOffset = 0;
    std::vector<size_t> checkpointLogStarts;
};

enum class ArenaSlabEncoding : uint8_t {
    RawBytes = 0,
    Structured = 1,
};

struct ArenaStructuredSlot {
    uint16_t offset = 0;
    std::string payload;
};

struct ArenaSlabImage {
    uint16_t slabIndex = 0;
    uint32_t count = 0;
    std::vector<size_t> inUse;
    ArenaSlabEncoding encoding = ArenaSlabEncoding::RawBytes;
    std::vector<std::byte> bytes;
    std::vector<ArenaStructuredSlot> structuredSlots;
};

struct ArenaMappedRawSlabAttachment {
    uint16_t slabIndex = 0;
    uint32_t count = 0;
    std::vector<size_t> inUse;
    std::shared_ptr<void> mappedRegion;
    size_t mappedRegionSize = 0;
    std::byte* items = nullptr;
    size_t itemsSizeBytes = 0;
};

struct ArenaMappedStructuredSlabAttachment {
    uint16_t slabIndex = 0;
    uint32_t count = 0;
    std::vector<size_t> inUse;
    std::vector<ArenaStructuredSlot> structuredSlots;
    std::shared_ptr<void> mappedRegion;
    size_t mappedRegionSize = 0;
    std::byte* items = nullptr;
    size_t itemsSizeBytes = 0;
};

enum class ArenaSlabBackingPolicy : uint8_t {
    Heap = 0,
    MmapFile = 1,
};

struct ArenaRootAnchor {
    std::string name;
    SlabId valueSid;
};

struct ArenaRootState {
    uint32_t version = 1;
    std::vector<ArenaRootAnchor> anchors;
};

std::string serializeArenaCheckpointMetadata(const ArenaCheckpointMetadata& metadata);
bool deserializeArenaCheckpointMetadata(std::string_view text, ArenaCheckpointMetadata& metadata);
bool deserializeArenaCheckpointMetadata(const std::string& text, ArenaCheckpointMetadata& metadata);
std::string serializeArenaSlabImage(const ArenaSlabImage& image);
bool deserializeArenaSlabImage(std::string_view text, ArenaSlabImage& image);
bool deserializeArenaSlabImage(const std::string& text, ArenaSlabImage& image);
std::string serializeArenaRootState(const ArenaRootState& rootState);
bool deserializeArenaRootState(std::string_view text, ArenaRootState& rootState);
bool deserializeArenaRootState(const std::string& text, ArenaRootState& rootState);

struct ArenaRestoreTag {};
inline constexpr ArenaRestoreTag kArenaRestoreTag{};

namespace arena_persistence_detail {

template<typename T>
inline void appendPod(std::string& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const char* raw = reinterpret_cast<const char*>(&value);
    out.append(raw, sizeof(T));
}

template<typename T>
inline bool readPod(const std::string& text, size_t& cursor, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (cursor + sizeof(T) > text.size()) {
        return false;
    }
    std::memcpy(&value, text.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

inline void appendString(std::string& out, const std::string& value) {
    size_t size = value.size();
    appendPod(out, size);
    out.append(value.data(), value.size());
}

inline bool readString(const std::string& text, size_t& cursor, std::string& value) {
    size_t size = 0;
    if (!readPod(text, cursor, size) || cursor + size > text.size()) {
        return false;
    }
    value.assign(text.data() + cursor, size);
    cursor += size;
    return true;
}

inline void appendBytes(std::string& out, const void* data, size_t size) {
    appendPod(out, size);
    if (size > 0) {
        out.append(reinterpret_cast<const char*>(data), size);
    }
}

inline bool readBytes(const std::string& text, size_t& cursor, std::vector<std::byte>& value) {
    size_t size = 0;
    if (!readPod(text, cursor, size) || cursor + size > text.size()) {
        return false;
    }
    value.resize(size);
    if (size > 0) {
        std::memcpy(value.data(), text.data() + cursor, size);
        cursor += size;
    }
    return true;
}

inline void appendSlabId(std::string& out, const SlabId& value) {
    appendPod(out, value.first);
    appendPod(out, value.second);
}

inline bool readSlabId(const std::string& text, size_t& cursor, SlabId& value) {
    return readPod(text, cursor, value.first) && readPod(text, cursor, value.second);
}

} // namespace arena_persistence_detail

template<typename T, typename Enable = void>
struct ArenaPersistenceTraits {
    static constexpr bool supported = false;
    static constexpr ArenaSlabEncoding encoding = ArenaSlabEncoding::Structured;
};

template<typename T, typename Enable = void>
struct ArenaWatermarkResetTraits {
    static constexpr bool strictEligible = std::is_trivially_destructible_v<T>;
    static void resetTransient(T&) {}
};

template<typename T, typename Enable = void>
struct ArenaMmapStructuredAttachTraits {
    static constexpr bool supported = false;
};

template<typename T>
struct ArenaPersistenceTraits<T, std::enable_if_t<std::is_trivially_copyable_v<T>>> {
    static constexpr bool supported = true;
    static constexpr ArenaSlabEncoding encoding = ArenaSlabEncoding::RawBytes;
};

class ArenaStore {
public:
    virtual ~ArenaStore() = default;
    virtual bool loadCurrent(ArenaCheckpointMetadata& out) = 0;
    virtual bool saveCurrent(const ArenaCheckpointMetadata& checkpoint) = 0;
    virtual bool saveNamedCheckpoint(const std::string& name, const ArenaCheckpointMetadata& checkpoint) = 0;
    virtual bool loadNamedCheckpoint(const std::string& name, ArenaCheckpointMetadata& out) = 0;
    virtual bool saveSlab(const ArenaSlabImage& slab) = 0;
    virtual bool loadSlab(uint16_t slabIndex, ArenaSlabImage& out) = 0;
    virtual bool saveRootState(const std::string& name, const ArenaRootState& rootState) = 0;
    virtual bool loadRootState(const std::string& name, ArenaRootState& out) = 0;
};

class MemoryArenaStore : public ArenaStore {
public:
    bool loadCurrent(ArenaCheckpointMetadata& out) override;
    bool saveCurrent(const ArenaCheckpointMetadata& checkpoint) override;
    bool saveNamedCheckpoint(const std::string& name, const ArenaCheckpointMetadata& checkpoint) override;
    bool loadNamedCheckpoint(const std::string& name, ArenaCheckpointMetadata& out) override;
    bool saveSlab(const ArenaSlabImage& slab) override;
    bool loadSlab(uint16_t slabIndex, ArenaSlabImage& out) override;
    bool saveRootState(const std::string& name, const ArenaRootState& rootState) override;
    bool loadRootState(const std::string& name, ArenaRootState& out) override;

private:
    bool hasCurrent = false;
    ArenaCheckpointMetadata current;
    std::map<std::string, ArenaCheckpointMetadata> named;
    std::map<uint16_t, ArenaSlabImage> slabs;
    std::map<std::string, ArenaRootState> rootStates;
};

class FileArenaStore : public ArenaStore {
public:
    explicit FileArenaStore(std::string filePath);

    bool loadCurrent(ArenaCheckpointMetadata& out) override;
    bool saveCurrent(const ArenaCheckpointMetadata& checkpoint) override;
    bool saveNamedCheckpoint(const std::string& name, const ArenaCheckpointMetadata& checkpoint) override;
    bool loadNamedCheckpoint(const std::string& name, ArenaCheckpointMetadata& out) override;
    bool saveSlab(const ArenaSlabImage& slab) override;
    bool loadSlab(uint16_t slabIndex, ArenaSlabImage& out) override;
    bool saveRootState(const std::string& name, const ArenaRootState& rootState) override;
    bool loadRootState(const std::string& name, ArenaRootState& out) override;

private:
    std::string path;
    bool readAll(bool& hasCurrentOut,
                 ArenaCheckpointMetadata& currentOut,
                 std::map<std::string, ArenaCheckpointMetadata>& namedOut) const;
    bool writeAll(bool hasCurrentValue,
                  const ArenaCheckpointMetadata& currentValue,
                  const std::map<std::string, ArenaCheckpointMetadata>& namedValue) const;
};

class BlobAllocator {
private:
    struct BlobSlab {
        size_t count;
        char* bytes;
        BlobSlab() : count(0), bytes(static_cast<char*>(malloc(65536))) {}
        ~BlobSlab() { free(bytes); }
    };

    BlobSlab *slabs[NUM_SLABS] = {nullptr};
    std::vector<SlabId> checkpointWatermarks;
    std::vector<size_t> checkpointStack;

    BlobAllocator() {}

public:
    static BlobAllocator& getAllocator() {
        static BlobAllocator instance;
        return instance;
    }

    ~BlobAllocator() {
        for (int i = 0; i < NUM_SLABS; ++i) {
            if (slabs[i]) {
                delete slabs[i];
                slabs[i] = nullptr;
            }
        }
    }

    SlabId allocate(const void* data, size_t length) {
        if (length == 0 || length > 65536) return SlabId(0, 0);

        for (uint16_t i = 0; i < NUM_SLABS; ++i) {
            if (!slabs[i]) {
                slabs[i] = new BlobSlab();
                if (i == 0) slabs[i]->count = 1; // reserve (0,0) as null sentinel
            }
            if (65536 - slabs[i]->count >= length) {
                SlabId id(i, static_cast<uint16_t>(slabs[i]->count));
                if (data) memcpy(&slabs[i]->bytes[slabs[i]->count], data, length);
                else memset(&slabs[i]->bytes[slabs[i]->count], 0, length);
                slabs[i]->count += length;
                return id;
            }
        }
        return SlabId(0, 0);
    }

    void* getPointer(SlabId id) const {
        if (!id || id.first >= NUM_SLABS || !slabs[id.first]) return nullptr;
        return &slabs[id.first]->bytes[id.second];
    }

    struct Checkpoint {
        size_t logStart;
        size_t depth;
        bool valid;
        SlabId watermark;
        bool appendOnly;
    };

    Checkpoint checkpoint() {
        SlabId current_watermark(0, 0);
        for (int i = NUM_SLABS - 1; i >= 0; --i) {
            if (slabs[i]) {
                current_watermark = SlabId(static_cast<uint16_t>(i), static_cast<uint16_t>(slabs[i]->count));
                break;
            }
        }
        checkpointWatermarks.push_back(current_watermark);
        checkpointStack.push_back(checkpointWatermarks.size());
        return {checkpointStack.size(), checkpointStack.size(), true, current_watermark, true};
    }

    bool rollback(const Checkpoint& cp) {
        if (!cp.valid || checkpointStack.empty() || cp.depth != checkpointStack.size()) return false;
        SlabId watermark = cp.watermark;
        checkpointWatermarks.pop_back();
        checkpointStack.pop_back();
        
        for (int i = watermark.first + 1; i < NUM_SLABS; ++i) {
            if (slabs[i]) slabs[i]->count = 0;
        }
        if (slabs[watermark.first]) {
            slabs[watermark.first]->count = watermark.second;
        }
        return true;
    }
    
    bool commit(const Checkpoint& cp) {
        if (!cp.valid || checkpointStack.empty() || cp.depth != checkpointStack.size()) return false;
        checkpointWatermarks.pop_back();
        checkpointStack.pop_back();
        return true;
    }
    
    void clearAllSlabs() {
        checkpointStack.clear();
        checkpointWatermarks.clear();
        for (int i = 0; i < NUM_SLABS; ++i) {
            if (slabs[i]) {
                delete slabs[i];
                slabs[i] = nullptr;
            }
        }
    }

    struct BlobStats {
        size_t activeSlabs = 0;
        size_t totalBytes  = 0;  // activeSlabs * 65536
        size_t usedBytes   = 0;  // sum of slabs[i]->count
    };

    BlobStats getBlobStats() const {
        BlobStats stats;
        for (int i = 0; i < NUM_SLABS; ++i) {
            if (!slabs[i] || slabs[i]->count == 0) continue;
            ++stats.activeSlabs;
            stats.usedBytes  += slabs[i]->count;
            stats.totalBytes += 65536;
        }
        return stats;
    }
};

template<typename T>
class Allocator {
private:
    mutable std::recursive_mutex mutex_;

    struct Slab {
        size_t count;
        std::vector<size_t> inUse;
        T *items;
        std::shared_ptr<void> mappedRegion;
        bool mappedBacking;
        // Fix initialization order to match declaration order
        Slab()
            : count(0),
              inUse(SLAB_SIZE, 0),
              items(static_cast<T*>(calloc(SLAB_SIZE, sizeof(T)))),
              mappedBacking(false) {}
        Slab(size_t initialCount,
             std::vector<size_t> initialInUse,
             T* mappedItems,
             std::shared_ptr<void> mappedRegionIn,
             size_t mappedSize)
            : count(initialCount),
              inUse(std::move(initialInUse)),
              items(mappedItems),
              mappedRegion(std::move(mappedRegionIn)),
              mappedBacking(true) {
            (void)mappedSize;
        }
        ~Slab() {
            if (!mappedBacking) {
                free(items);
            }
        }
    };

    Slab *slabs[NUM_SLABS] = {nullptr};
    std::vector<SlabId> allocationLog;
    ArenaSlabBackingPolicy backingPolicy_ = ArenaSlabBackingPolicy::Heap;
    std::string mmapBackingDirectory_;
    std::string mmapBackingFilePrefix_;
    std::vector<size_t> checkpointStack;
    std::vector<SlabId> checkpointWatermarks;
    std::vector<bool> checkpointAppendOnly;
    std::unordered_set<SlabId> rollbackProtectedFreeSlots;
    bool lastRollbackUsedFastPath = false;
    bool lastRollbackUsedStrictFastPath = false;

    void clearSlabsOnly() {
        for (size_t i = 0; i < NUM_SLABS; ++i) {
            if (slabs[i]) {
                delete slabs[i];
                slabs[i] = nullptr;
            }
        }
    }

    void clearAllSlabs() {
        checkpointStack.clear();
        checkpointWatermarks.clear();
        checkpointAppendOnly.clear();
        allocationLog.clear();
        rollbackProtectedFreeSlots.clear();
        lastRollbackUsedFastPath = false;
        lastRollbackUsedStrictFastPath = false;
        clearSlabsOnly();
    }

    void invalidateCheckpointFastPath() {
        for (size_t i = 0; i < checkpointAppendOnly.size(); ++i) {
            checkpointAppendOnly[i] = false;
        }
    }

    SlabId currentHighWatermark() const {
        for (int slabIndex = NUM_SLABS - 1; slabIndex >= 0; --slabIndex) {
            if (!slabs[slabIndex]) {
                continue;
            }
            for (int offset = SLAB_SIZE - 1; offset >= 0; --offset) {
                SlabId sid(static_cast<uint16_t>(slabIndex), static_cast<uint16_t>(offset));
                if (refs(sid) > 0) {
                    return sid;
                }
            }
        }
        return {};
    }

    std::string mmapBackingPathForSlab(uint16_t slabIndex) const {
        std::ostringstream name;
        name << (mmapBackingFilePrefix_.empty() ? std::string("slab") : mmapBackingFilePrefix_)
             << ".slab.";
        name.width(4);
        name.fill('0');
        name << slabIndex << ".bin";
        return (std::filesystem::path(mmapBackingDirectory_) / name.str()).string();
    }

    Slab* createOwnedSlab(uint16_t slabIndex) {
        if constexpr (ArenaPersistenceTraits<T>::encoding == ArenaSlabEncoding::RawBytes) {
            if (backingPolicy_ == ArenaSlabBackingPolicy::MmapFile) {
                if (mmapBackingDirectory_.empty()) {
                    throw std::runtime_error("mmap slab backing directory is not configured");
                }
                std::error_code ec;
                std::filesystem::create_directories(mmapBackingDirectory_, ec);
                if (ec) {
                    throw std::runtime_error("failed to create mmap slab backing directory");
                }
                const auto path = mmapBackingPathForSlab(slabIndex);
                const size_t bytes = SLAB_SIZE * sizeof(T);
                const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    throw std::runtime_error("failed to open mmap slab backing file");
                }
                if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
                    ::close(fd);
                    throw std::runtime_error("failed to size mmap slab backing file");
                }
                void* mapped = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
                ::close(fd);
                if (mapped == MAP_FAILED) {
                    throw std::runtime_error("failed to mmap slab backing file");
                }
                std::shared_ptr<void> region(mapped, [bytes](void* ptr) {
                    if (ptr && ptr != MAP_FAILED) {
                        ::msync(ptr, bytes, MS_SYNC);
                        ::munmap(ptr, bytes);
                    }
                });
                T* mappedItems = reinterpret_cast<T*>(region.get());
                return new Slab(0, std::vector<size_t>(SLAB_SIZE, 0), mappedItems, std::move(region), bytes);
            }
        }
        (void)slabIndex;
        return new Slab();
    }

    SlabId allocateAppendOnlyRaw() {
        SlabId watermark = currentHighWatermark();
        uint16_t slabIndex = watermark ? watermark.first : 0;

        for (; slabIndex < NUM_SLABS; ++slabIndex) {
            if (!slabs[slabIndex]) {
                slabs[slabIndex] = createOwnedSlab(slabIndex);
            }

            uint16_t startOffset = 0;
            if (watermark && slabIndex == watermark.first) {
                startOffset = static_cast<uint16_t>(watermark.second + 1);
            } else if (!watermark && slabIndex == 0) {
                startOffset = 1;
            }

            for (uint16_t offset = startOffset; offset < SLAB_SIZE; ++offset) {
                SlabId sid(slabIndex, offset);
                if (!sid) {
                    continue;
                }
                if (refs(sid) == 0 && rollbackProtectedFreeSlots.find(sid) == rollbackProtectedFreeSlots.end()) {
                    return sid;
                }
            }
        }

        throw std::bad_alloc();
    }

    void rollbackAppendOnlyFromWatermark(SlabId watermark) {
        SlabId current = currentHighWatermark();
        while (current && current != watermark) {
            if (slabs[current.first] && refs(current) > 0) {
                slabs[current.first]->inUse[current.second] = 1;
                deallocate(current);
            }

            if (current.second == 0) {
                if (current.first == 0) {
                    break;
                }
                current = SlabId(static_cast<uint16_t>(current.first - 1), SLAB_SIZE - 1);
            } else {
                current = SlabId(current.first, static_cast<uint16_t>(current.second - 1));
            }
        }
    }

    bool canUseStrictWatermarkReset() const {
        return ArenaWatermarkResetTraits<T>::strictEligible;
    }

    void rollbackStrictAppendOnlyFromWatermark(SlabId watermark) {
        for (uint16_t slabIndex = static_cast<uint16_t>(watermark.first + 1); slabIndex < NUM_SLABS; ++slabIndex) {
            if (!slabs[slabIndex]) {
                continue;
            }
            for (size_t offset = 0; offset < SLAB_SIZE; ++offset) {
                if (slabs[slabIndex]->inUse[offset] > 0) {
                    ArenaWatermarkResetTraits<T>::resetTransient(slabs[slabIndex]->items[offset]);
                }
            }
            std::fill(slabs[slabIndex]->inUse.begin(), slabs[slabIndex]->inUse.end(), 0);
            slabs[slabIndex]->count = 0;
        }

        if (!watermark) {
            if (slabs[0]) {
                std::fill(slabs[0]->inUse.begin() + 1, slabs[0]->inUse.end(), 0);
                slabs[0]->count = refs(SlabId(0, 0)) > 0 ? 1 : 0;
            }
            return;
        }

        if (slabs[watermark.first]) {
            const size_t start = static_cast<size_t>(watermark.second) + 1;
            for (size_t offset = start; offset < SLAB_SIZE; ++offset) {
                if (slabs[watermark.first]->inUse[offset] > 0) {
                    ArenaWatermarkResetTraits<T>::resetTransient(slabs[watermark.first]->items[offset]);
                }
            }
            std::fill(slabs[watermark.first]->inUse.begin() + start, slabs[watermark.first]->inUse.end(), 0);
            slabs[watermark.first]->count = start;
        }
    }

    void rollbackAllocation(SlabId si) {
        if (si.first >= NUM_SLABS || !slabs[si.first] || si.second >= SLAB_SIZE) {
            return;
        }

        if (slabs[si.first]->inUse[si.second] == 0) {
            return;
        }

        // First-version transaction support is allocation-oriented: objects created
        // after the checkpoint are treated as transient scratch state and discarded
        // wholesale during rollback.
        slabs[si.first]->inUse[si.second] = 1;
        deallocate(si);
    }

public:
    struct Checkpoint {
        size_t logStart = 0;
        size_t depth = 0;
        bool valid = false;
        SlabId watermark;
        bool appendOnlyEligible = false;
    };

    static Allocator<T> &getAllocator() {
        static Allocator<T> allocator_instance;
        static SlabId sentinel = allocator_instance.allocRaw(); // "nullptr"
        (void)sentinel;
        return allocator_instance;
    }

    Allocator() { LOG_ALLOC("constructed allocator for " << typeid(T).name()); }

    ~Allocator() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        LOG_ALLOC("destructed allocator for " << typeid(T).name());
        for (size_t i = 0; i < NUM_SLABS; ++i) {
            if (slabs[i]) {
                delete slabs[i];
            }
        }
    }

    template<typename... Args>
    SlabId allocate(Args &&...args) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        SlabId si = allocRaw();
        new (&slabs[si.first]->items[si.second]) T(std::forward<Args>(args)...);
        return si;
    }

    SlabId allocRaw() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        SlabId si;
        if (!checkpointStack.empty()) {
            si = allocateAppendOnlyRaw();
            goto allocated;
        }
        for (si.first = 0, si.second = 0; si.first < NUM_SLABS; ++si.first) {
            if (slabs[si.first]) {
                for (si.second = 0; slabs[si.first]->count < SLAB_SIZE && si.second < SLAB_SIZE; ++si.second) {
                    if (refs(si) == 0 && rollbackProtectedFreeSlots.find(si) == rollbackProtectedFreeSlots.end()) {
                        goto allocated;
                    }
                }
            } else { // allocate first unallocated slab
                slabs[si.first] = createOwnedSlab(si.first);
                goto allocated;
            }
        }

        throw std::bad_alloc(); // No more items available

    allocated:
        ++slabs[si.first]->count;
        modrefs(si);
        if (!checkpointStack.empty()) {
            allocationLog.push_back(si);
        }
        LOG_ALLOC("allocated " << typeid(T).name() << " SlabId " << si);
        return si;
    }

    Checkpoint checkpoint() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        checkpointStack.push_back(allocationLog.size());
        checkpointWatermarks.push_back(currentHighWatermark());
        checkpointAppendOnly.push_back(true);
        lastRollbackUsedFastPath = false;
        lastRollbackUsedStrictFastPath = false;
        return {allocationLog.size(), checkpointStack.size(), true, checkpointWatermarks.back(), checkpointAppendOnly.back()};
    }

    bool hasActiveCheckpoint() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return !checkpointStack.empty();
    }

    Checkpoint currentCheckpoint() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (checkpointStack.empty()) {
            return {};
        }
        return {checkpointStack.back(), checkpointStack.size(), true, checkpointWatermarks.back(), checkpointAppendOnly.back()};
    }

    void setSlabBackingPolicy(ArenaSlabBackingPolicy policy) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        backingPolicy_ = policy;
    }

    ArenaSlabBackingPolicy slabBackingPolicy() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return backingPolicy_;
    }

    void configureMmapFileBackedSlabs(std::string directory,
                                      std::string filePrefix = std::string()) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        mmapBackingDirectory_ = std::move(directory);
        mmapBackingFilePrefix_ = std::move(filePrefix);
    }

    std::string slabBackingPath(uint16_t slabIndex) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (backingPolicy_ != ArenaSlabBackingPolicy::MmapFile || mmapBackingDirectory_.empty()) {
            return std::string();
        }
        return mmapBackingPathForSlab(slabIndex);
    }

    bool slabUsesMappedBacking(uint16_t slabIndex) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return slabIndex < NUM_SLABS && slabs[slabIndex] && slabs[slabIndex]->mappedBacking;
    }

    bool flushMappedSlabs() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (size_t slabIndex = 0; slabIndex < NUM_SLABS; ++slabIndex) {
            if (!slabs[slabIndex] || !slabs[slabIndex]->mappedBacking || !slabs[slabIndex]->items) {
                continue;
            }
            if (::msync(slabs[slabIndex]->items, SLAB_SIZE * sizeof(T), MS_SYNC) != 0) {
                return false;
            }
        }
        return true;
    }

    ArenaCheckpointMetadata exportArenaMetadata() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        ArenaCheckpointMetadata metadata;
        metadata.allocationLogSize = allocationLog.size();
        metadata.checkpointLogStarts = checkpointStack;

        SlabId highest;
        for (size_t slabIndex = 0; slabIndex < NUM_SLABS; ++slabIndex) {
            if (!slabs[slabIndex]) {
                continue;
            }

            bool slabHasLiveUserSlot = false;
            for (size_t offset = 0; offset < SLAB_SIZE; ++offset) {
                SlabId sid(static_cast<uint16_t>(slabIndex), static_cast<uint16_t>(offset));
                if (!sid || refs(sid) == 0) {
                    continue;
                }

                ++metadata.liveSlotCount;
                highest = sid;
                slabHasLiveUserSlot = true;
            }

            if (slabHasLiveUserSlot) {
                ++metadata.activeSlabCount;
            }
        }

        metadata.highestSlabIndex = highest.first;
        metadata.highestSlabOffset = highest.second;
        return metadata;
    }

    // Lightweight per-slab utilization stats (no data copying).
    struct SlabStat {
        uint16_t slabIndex = 0;
        uint32_t liveSlots = 0;
    };
    struct AllocatorStats {
        uint32_t liveSlotCount      = 0;
        uint32_t activeSlabCount    = 0;
        uint32_t totalCapacitySlots = 0;  // activeSlabCount * SLAB_SIZE
        size_t   itemSizeBytes      = 0;  // sizeof(T)
        std::vector<SlabStat> slabs;
    };

    AllocatorStats getStats() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        AllocatorStats stats;
        stats.itemSizeBytes = sizeof(T);
        for (size_t slabIndex = 0; slabIndex < NUM_SLABS; ++slabIndex) {
            if (!slabs[slabIndex]) continue;
            uint32_t live = 0;
            for (size_t offset = 0; offset < SLAB_SIZE; ++offset) {
                SlabId sid(static_cast<uint16_t>(slabIndex), static_cast<uint16_t>(offset));
                if (sid && refs(sid) > 0) ++live;
            }
            if (live == 0) continue;
            ++stats.activeSlabCount;
            stats.liveSlotCount      += live;
            stats.totalCapacitySlots += SLAB_SIZE;
            stats.slabs.push_back({static_cast<uint16_t>(slabIndex), live});
        }
        return stats;
    }

    bool restoreArenaMetadata(const ArenaCheckpointMetadata& metadata) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (metadata.version != 1 || metadata.slabSize != SLAB_SIZE) {
            return false;
        }

        auto current = exportArenaMetadata();
        if (current.liveSlotCount != 0 || current.checkpointLogStarts.size() != 0 || current.allocationLogSize != 0) {
            return false;
        }

        checkpointStack = metadata.checkpointLogStarts;
        checkpointWatermarks.assign(checkpointStack.size(), SlabId());
        checkpointAppendOnly.assign(checkpointStack.size(), false);
        lastRollbackUsedFastPath = false;
        lastRollbackUsedStrictFastPath = false;
        return true;
    }

    std::vector<ArenaSlabImage> exportSlabImages() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<ArenaSlabImage> out;
        if constexpr (!ArenaPersistenceTraits<T>::supported) {
            return out;
        }

        for (size_t slabIndex = 0; slabIndex < NUM_SLABS; ++slabIndex) {
            if (!slabs[slabIndex]) {
                continue;
            }

            bool hasUserSlot = false;
            for (size_t offset = 1; offset < SLAB_SIZE; ++offset) {
                SlabId sid(static_cast<uint16_t>(slabIndex), static_cast<uint16_t>(offset));
                if (refs(sid) > 0) {
                    hasUserSlot = true;
                    break;
                }
            }
            if (!hasUserSlot) {
                continue;
            }

            ArenaSlabImage image;
            image.slabIndex = static_cast<uint16_t>(slabIndex);
            image.count = static_cast<uint32_t>(slabs[slabIndex]->count);
            image.inUse = slabs[slabIndex]->inUse;
            image.encoding = ArenaPersistenceTraits<T>::encoding;
            if constexpr (ArenaPersistenceTraits<T>::encoding == ArenaSlabEncoding::RawBytes) {
                image.bytes.resize(SLAB_SIZE * sizeof(T));
                std::memcpy(image.bytes.data(), slabs[slabIndex]->items, image.bytes.size());
            } else {
                for (size_t offset = 0; offset < SLAB_SIZE; ++offset) {
                    SlabId sid(static_cast<uint16_t>(slabIndex), static_cast<uint16_t>(offset));
                    if (refs(sid) == 0) {
                        continue;
                    }
                    ArenaStructuredSlot slot;
                    slot.offset = static_cast<uint16_t>(offset);
                    if (!ArenaPersistenceTraits<T>::exportSlot(slabs[slabIndex]->items[offset], slot.payload)) {
                        return {};
                    }
                    image.structuredSlots.push_back(std::move(slot));
                }
            }
            out.push_back(std::move(image));
        }
        return out;
    }

    std::vector<ArenaSlabImage> exportSlabImagesSince(const Checkpoint& checkpoint) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        std::vector<ArenaSlabImage> out;
        if constexpr (!ArenaPersistenceTraits<T>::supported) {
            return out;
        }
        if (!checkpoint.valid || !checkpoint.appendOnlyEligible) {
            return out;
        }

        const auto isAfterWatermark = [&](const SlabId& sid) {
            if (!checkpoint.watermark) {
                return static_cast<bool>(sid);
            }
            return sid.first > checkpoint.watermark.first ||
                   (sid.first == checkpoint.watermark.first && sid.second > checkpoint.watermark.second);
        };

        for (size_t slabIndex = 0; slabIndex < NUM_SLABS; ++slabIndex) {
            if (!slabs[slabIndex]) {
                continue;
            }

            bool hasUserSlot = false;
            std::vector<size_t> filtered_in_use = slabs[slabIndex]->inUse;
            for (size_t offset = 0; offset < SLAB_SIZE; ++offset) {
                SlabId sid(static_cast<uint16_t>(slabIndex), static_cast<uint16_t>(offset));
                if (!isAfterWatermark(sid)) {
                    filtered_in_use[offset] = 0;
                    continue;
                }
                if (refs(sid) > 0) {
                    hasUserSlot = true;
                } else {
                    filtered_in_use[offset] = 0;
                }
            }
            if (!hasUserSlot) {
                continue;
            }

            ArenaSlabImage image;
            image.slabIndex = static_cast<uint16_t>(slabIndex);
            image.count = static_cast<uint32_t>(slabs[slabIndex]->count);
            image.inUse = std::move(filtered_in_use);
            image.encoding = ArenaPersistenceTraits<T>::encoding;
            if constexpr (ArenaPersistenceTraits<T>::encoding == ArenaSlabEncoding::RawBytes) {
                image.bytes.resize(SLAB_SIZE * sizeof(T));
                std::memcpy(image.bytes.data(), slabs[slabIndex]->items, image.bytes.size());
            } else {
                for (size_t offset = 0; offset < SLAB_SIZE; ++offset) {
                    SlabId sid(static_cast<uint16_t>(slabIndex), static_cast<uint16_t>(offset));
                    if (!isAfterWatermark(sid) || refs(sid) == 0) {
                        continue;
                    }
                    ArenaStructuredSlot slot;
                    slot.offset = static_cast<uint16_t>(offset);
                    if (!ArenaPersistenceTraits<T>::exportSlot(slabs[slabIndex]->items[offset], slot.payload)) {
                        return {};
                    }
                    image.structuredSlots.push_back(std::move(slot));
                }
            }
            out.push_back(std::move(image));
        }
        return out;
    }

    bool restoreSlabImages(const std::vector<ArenaSlabImage>& images) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if constexpr (!ArenaPersistenceTraits<T>::supported) {
            return false;
        }

        clearSlabsOnly();

        for (const auto& image : images) {
            if (image.slabIndex >= NUM_SLABS || image.inUse.size() != SLAB_SIZE || image.encoding != ArenaPersistenceTraits<T>::encoding) {
                return false;
            }

            slabs[image.slabIndex] = new Slab();
            slabs[image.slabIndex]->count = image.count;
            slabs[image.slabIndex]->inUse = image.inUse;
            if constexpr (ArenaPersistenceTraits<T>::encoding == ArenaSlabEncoding::RawBytes) {
                if (image.bytes.size() != SLAB_SIZE * sizeof(T)) {
                    return false;
                }
                std::memcpy(slabs[image.slabIndex]->items, image.bytes.data(), image.bytes.size());
            } else {
                std::vector<bool> restored(SLAB_SIZE, false);
                for (const auto& slot : image.structuredSlots) {
                    if (slot.offset >= SLAB_SIZE || image.inUse[slot.offset] == 0 || restored[slot.offset]) {
                        return false;
                    }
                    if (!ArenaPersistenceTraits<T>::restoreSlot(&slabs[image.slabIndex]->items[slot.offset], slot.payload)) {
                        return false;
                    }
                    restored[slot.offset] = true;
                }
                for (size_t offset = 0; offset < SLAB_SIZE; ++offset) {
                    if (image.inUse[offset] > 0 && !restored[offset]) {
                        return false;
                    }
                }
            }
        }

        if (!slabs[0]) {
            SlabId nullSentinel = allocRaw();
            (void)nullSentinel;
            allocationLog.clear();
        }
        return true;
    }

    bool attachMappedRawSlabs(const std::vector<ArenaMappedRawSlabAttachment>& attachments) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if constexpr (!ArenaPersistenceTraits<T>::supported || ArenaPersistenceTraits<T>::encoding != ArenaSlabEncoding::RawBytes) {
            return false;
        }

        clearSlabsOnly();

        for (const auto& attachment : attachments) {
            if (attachment.slabIndex >= NUM_SLABS ||
                attachment.inUse.size() != SLAB_SIZE ||
                attachment.count > SLAB_SIZE ||
                attachment.items == nullptr ||
                attachment.itemsSizeBytes != SLAB_SIZE * sizeof(T) ||
                attachment.mappedRegion == nullptr) {
                return false;
            }

            slabs[attachment.slabIndex] = new Slab(
                attachment.count,
                attachment.inUse,
                reinterpret_cast<T*>(attachment.items),
                attachment.mappedRegion,
                attachment.mappedRegionSize);
        }

        if (!slabs[0]) {
            SlabId nullSentinel = allocRaw();
            (void)nullSentinel;
            allocationLog.clear();
        }
        return true;
    }

    bool attachMappedStructuredSlabs(const std::vector<ArenaMappedStructuredSlabAttachment>& attachments) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if constexpr (!ArenaMmapStructuredAttachTraits<T>::supported) {
            return false;
        }

        clearSlabsOnly();

        for (const auto& attachment : attachments) {
            if (attachment.slabIndex >= NUM_SLABS ||
                attachment.inUse.size() != SLAB_SIZE ||
                attachment.count > SLAB_SIZE ||
                attachment.items == nullptr ||
                attachment.itemsSizeBytes != SLAB_SIZE * sizeof(T) ||
                attachment.mappedRegion == nullptr) {
                return false;
            }

            auto* slab = new Slab(
                attachment.count,
                attachment.inUse,
                reinterpret_cast<T*>(attachment.items),
                attachment.mappedRegion,
                attachment.mappedRegionSize);
            std::vector<bool> restored(SLAB_SIZE, false);
            for (const auto& slot : attachment.structuredSlots) {
                if (slot.offset >= SLAB_SIZE || attachment.inUse[slot.offset] == 0 || restored[slot.offset]) {
                    delete slab;
                    return false;
                }
                if (!ArenaPersistenceTraits<T>::restoreSlot(&slab->items[slot.offset], slot.payload)) {
                    delete slab;
                    return false;
                }
                restored[slot.offset] = true;
            }
            for (size_t offset = 0; offset < SLAB_SIZE; ++offset) {
                if (attachment.inUse[offset] > 0 && !restored[offset]) {
                    delete slab;
                    return false;
                }
            }
            slabs[attachment.slabIndex] = slab;
        }

        if (!slabs[0]) {
            SlabId nullSentinel = allocRaw();
            (void)nullSentinel;
            allocationLog.clear();
        }
        return true;
    }

    void resetForTests() {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        clearAllSlabs();
        backingPolicy_ = ArenaSlabBackingPolicy::Heap;
        mmapBackingDirectory_.clear();
        mmapBackingFilePrefix_.clear();
        SlabId sentinel = allocRaw();
        (void)sentinel;
        allocationLog.clear();
        checkpointStack.clear();
        checkpointWatermarks.clear();
        checkpointAppendOnly.clear();
        lastRollbackUsedFastPath = false;
        lastRollbackUsedStrictFastPath = false;
    }

    bool commit(const Checkpoint& checkpoint) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!checkpoint.valid || checkpointStack.empty() || checkpoint.depth != checkpointStack.size() ||
            checkpoint.logStart != checkpointStack.back()) {
            return false;
        }

        checkpointStack.pop_back();
        checkpointWatermarks.pop_back();
        checkpointAppendOnly.pop_back();
        if (checkpointStack.empty()) {
            allocationLog.clear();
            rollbackProtectedFreeSlots.clear();
        }
        lastRollbackUsedStrictFastPath = false;
        return true;
    }

    bool rollback(const Checkpoint& checkpoint) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!checkpoint.valid || checkpointStack.empty() || checkpoint.depth != checkpointStack.size() ||
            checkpoint.logStart != checkpointStack.back()) {
            return false;
        }

        bool useFastPath = !checkpointAppendOnly.empty() && checkpointAppendOnly.back();
        lastRollbackUsedFastPath = useFastPath;
        lastRollbackUsedStrictFastPath = false;
        if (useFastPath) {
            if (canUseStrictWatermarkReset()) {
                rollbackStrictAppendOnlyFromWatermark(checkpoint.watermark);
                lastRollbackUsedStrictFastPath = true;
            } else {
                rollbackAppendOnlyFromWatermark(checkpoint.watermark);
            }
        } else {
            for (size_t i = allocationLog.size(); i-- > checkpoint.logStart;) {
                rollbackAllocation(allocationLog[i]);
            }
        }

        allocationLog.resize(checkpoint.logStart);
        checkpointStack.pop_back();
        checkpointWatermarks.pop_back();
        checkpointAppendOnly.pop_back();
        if (checkpointStack.empty()) {
            allocationLog.clear();
            rollbackProtectedFreeSlots.clear();
        }
        return true;
    }

    bool deallocate(SlabId si) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (valid(si)) {
            // Decrement reference count
            Slab *slab = slabs[si.first];
            modrefs(si, -1);
            LOG_ALLOC("dereferenced " << typeid(T).name() << " SlabId " << si << ", refs now: " << refs(si));

            // Only deallocate if reference count reaches 0
            if (slab->inUse[si.second] == 0) {
                if (!checkpointStack.empty()) {
                    rollbackProtectedFreeSlots.insert(si);
                    invalidateCheckpointFastPath();
                }
                LOG_ALLOC("deleted " << typeid(T).name() << " SlabId " << si);
                slab->items[si.second].~T();
                --slab->count;

                if (slab->count == 0) {
                    LOG_ALLOC("deleted " << typeid(T).name()  << " Slab " << si.first);
                    delete slab;
                    slabs[si.first] = nullptr;
                    return true;
                }
            }
        }
        return false;
    }

    T *getPtr(SlabId si) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return valid(si) ? &(slabs[si.first]->items[si.second]) : nullptr;
    }

    SlabId getSlabId(const T *ptr) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        SlabId si;
        for (si.first = 0; si.first < NUM_SLABS; ++si.first) {
            if (slabs[si.first]) {
                T *slabStart = slabs[si.first]->items;
                T *slabEnd = slabStart + SLAB_SIZE;

                if (ptr >= slabStart && ptr < slabEnd) {
                    si.second = ptr - slabStart;
                    return si;
                }
            }
        }
        throw std::invalid_argument("Pointer not found in any slab");
    }

    bool valid(SlabId si) const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!si) return false;
        if (si.first < NUM_SLABS && slabs[si.first] && si.second < SLAB_SIZE) {
            return refs(si) > 0;
        }
        return false;
    }

    void modrefs(SlabId si, int delta = 1) { 
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (si.first < NUM_SLABS && slabs[si.first] && si.second < SLAB_SIZE) {
            slabs[si.first]->inUse[si.second] += delta; 
        }
    }

    bool tryRetain(SlabId si) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!si) return false;
        if (si.first < NUM_SLABS && slabs[si.first] && si.second < SLAB_SIZE && slabs[si.first]->inUse[si.second] > 0) {
            slabs[si.first]->inUse[si.second] += 1;
            return true;
        }
        return false;
    }
    
    size_t refs(const SlabId si) const { 
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (si.first < NUM_SLABS && slabs[si.first] && si.second < SLAB_SIZE) {
            return slabs[si.first]->inUse[si.second]; 
        }
        return 0;
    }

    bool lastRollbackUsedWatermarkFastPath() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return lastRollbackUsedFastPath;
    }

    bool lastRollbackUsedStrictWatermarkFastPath() const {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        return lastRollbackUsedStrictFastPath;
    }
};

template<typename T>
class CPtr {
private:
    SlabId slabId;

    // Prevent heap allocation of CPtr objects
    void* operator new(size_t) = delete;
    void* operator new[](size_t) = delete;
    void operator delete(void*) = delete;
    void operator delete[](void*) = delete;

public:
    const SlabId &getSlabId() const { return slabId; }

    // Release ownership: return the SlabId and zero this CPtr so the destructor
    // skips deallocation. Caller takes responsibility for the reference count.
    SlabId release() {
        SlabId si = slabId;
        slabId = SlabId();
        return si;
    }

    struct AdoptRawSlabIdTag {};

    CPtr() : slabId() {}
    CPtr(std::nullptr_t) : slabId() {}
    CPtr(AdoptRawSlabIdTag, const SlabId& other) : slabId(other) {}

    CPtr(const SlabId &other) {
        slabId = Allocator<T>::getAllocator().tryRetain(other) ? other : SlabId();
    }

    CPtr(const T *ptr) : CPtr(Allocator<T>::getAllocator().getSlabId(ptr)) {}

    static CPtr<T> adoptRaw(const SlabId& other) {
        return CPtr<T>(AdoptRawSlabIdTag{}, other);
    }
    
    CPtr(const CPtr<T> &other) : CPtr(other.slabId) {}
    
    CPtr(CPtr<T> &&other) noexcept : slabId(other.slabId) {
        other.slabId = SlabId();
    }

    CPtr& operator=(const CPtr<T> &other) {
        if (this != &other) {
            if (Allocator<T>::getAllocator().valid(slabId)) {
                Allocator<T>::getAllocator().modrefs(slabId, -1);
            }
            slabId = Allocator<T>::getAllocator().tryRetain(other.slabId) ? other.slabId : SlabId();
        }
        return *this;
    }

    CPtr& operator=(CPtr<T> &&other) noexcept {
        if (this != &other) {
            if (Allocator<T>::getAllocator().valid(slabId)) {
                Allocator<T>::getAllocator().deallocate(slabId);
            }
            slabId = other.slabId;
            other.slabId = SlabId();
        }
        return *this;
    }

    template<typename... Args,
             typename = std::enable_if_t<
                 !std::disjunction_v<std::is_same<std::remove_cv_t<std::remove_reference_t<Args>>, SlabId>...,
                                     std::is_same<std::remove_cv_t<std::remove_reference_t<Args>>, T *>...,
                                     std::is_same<std::remove_cv_t<std::remove_reference_t<Args>>, const T *>...,
                                     std::is_same<std::remove_cv_t<std::remove_reference_t<Args>>, CPtr<T>>...,
                                     std::is_same<std::remove_cv_t<std::remove_reference_t<Args>>, const CPtr<T>>...>>>
    CPtr(Args &&...args) : slabId(Allocator<T>::getAllocator().allocate(std::forward<Args>(args)...)) {}

    // Primary template - handles types without unwind()
    template<typename, typename = void>
    struct has_unwind : std::false_type {};

    // Specialization - handles types with static unwind(SlabId)
    template<typename U>
    struct has_unwind<U, std::void_t<decltype(U::unwind(std::declval<SlabId>()))>>
        : std::is_same<decltype(U::unwind(std::declval<SlabId>())), bool> {};

    ~CPtr() {
        if (Allocator<T>::getAllocator().valid(slabId)) {
            if (Allocator<T>::getAllocator().deallocate(slabId)) {
                // Was last reference, object is being deleted. 
                // In J3, destructors handle buffer cleanup.
            }
        }
    }

    // Check if this object is stored in a container
    bool isStored() const {
        return Allocator<T>::getAllocator().refs(slabId) > 1;
    }

    auto modrefs(int delta = 1) const { 
        return Allocator<T>::getAllocator().modrefs(slabId, delta); 
    }
    
    auto refs() const { 
        return Allocator<T>::getAllocator().refs(slabId);
    }

    operator T *() const { return Allocator<T>::getAllocator().getPtr(slabId); }
    T &operator*() const { return *Allocator<T>::getAllocator().getPtr(slabId); }
    T *operator->() const { return Allocator<T>::getAllocator().getPtr(slabId); }

    bool operator==(const CPtr<T> &other) const { return slabId == other.slabId; }
    bool operator!=(const CPtr<T> &other) const { return !(slabId == other.slabId); }
    explicit operator bool() const { return Allocator<T>::getAllocator().valid(slabId); }

    friend std::ostream &operator<<(std::ostream &os, const CPtr<T> &obj) {
        os << obj.slabId << "(" << Allocator<T>::getAllocator().refs(obj.slabId) << ")";
        return os;
    }
};
