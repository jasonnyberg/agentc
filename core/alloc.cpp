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

#include "alloc.h"

#include <cstring>
#include <dlfcn.h>
#include <filesystem>

namespace {

std::string encodeCheckpointStarts(const std::vector<size_t>& starts) {
    std::ostringstream out;
    for (size_t i = 0; i < starts.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << starts[i];
    }
    return out.str();
}

bool decodeCheckpointStarts(const std::string& text, std::vector<size_t>& out) {
    out.clear();
    if (text.empty()) {
        return true;
    }

    std::stringstream stream(text);
    std::string part;
    while (std::getline(stream, part, ',')) {
        if (part.empty()) {
            return false;
        }
        out.push_back(static_cast<size_t>(std::strtoull(part.c_str(), nullptr, 10)));
    }
    return true;
}

std::string encodeMetadata(const ArenaCheckpointMetadata& metadata) {
    std::ostringstream out;
    out << metadata.version << ' '
        << metadata.slabSize << ' '
        << metadata.activeSlabCount << ' '
        << metadata.liveSlotCount << ' '
        << metadata.allocationLogSize << ' '
        << metadata.highestSlabIndex << ' '
        << metadata.highestSlabOffset << ' '
        << encodeCheckpointStarts(metadata.checkpointLogStarts);
    return out.str();
}

bool decodeMetadata(const std::string& text, ArenaCheckpointMetadata& metadata) {
    std::istringstream in(text);
    std::string starts;
    if (!(in >> metadata.version >> metadata.slabSize >> metadata.activeSlabCount >>
          metadata.liveSlotCount >> metadata.allocationLogSize >> metadata.highestSlabIndex >>
          metadata.highestSlabOffset)) {
        return false;
    }

    std::getline(in >> std::ws, starts);
    return decodeCheckpointStarts(starts, metadata.checkpointLogStarts);
}

std::string encodeSlabImage(const ArenaSlabImage& image) {
    std::string out;
    uint8_t encoding = static_cast<uint8_t>(image.encoding);
    out.append(reinterpret_cast<const char*>(&encoding), sizeof(uint8_t));
    out.append(reinterpret_cast<const char*>(&image.slabIndex), sizeof(uint16_t));
    out.append(reinterpret_cast<const char*>(&image.count), sizeof(uint32_t));
    size_t inUseSize = image.inUse.size();
    out.append(reinterpret_cast<const char*>(&inUseSize), sizeof(size_t));
    if (!image.inUse.empty()) {
        out.append(reinterpret_cast<const char*>(image.inUse.data()), image.inUse.size() * sizeof(size_t));
    }

    if (image.encoding == ArenaSlabEncoding::RawBytes) {
        if (!image.bytes.empty()) {
            out.append(reinterpret_cast<const char*>(image.bytes.data()), image.bytes.size());
        }
        return out;
    }

    size_t slotCount = image.structuredSlots.size();
    out.append(reinterpret_cast<const char*>(&slotCount), sizeof(size_t));
    for (const auto& slot : image.structuredSlots) {
        out.append(reinterpret_cast<const char*>(&slot.offset), sizeof(uint16_t));
        size_t payloadSize = slot.payload.size();
        out.append(reinterpret_cast<const char*>(&payloadSize), sizeof(size_t));
        out.append(slot.payload.data(), slot.payload.size());
    }
    return out;
}

bool decodeSlabImage(const std::string& text, ArenaSlabImage& image) {
    image = {};
    if (text.size() < sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(size_t)) {
        return false;
    }

    const char* cursor = text.data();
    uint8_t encoding = 0;
    std::memcpy(&encoding, cursor, sizeof(uint8_t));
    cursor += sizeof(uint8_t);
    image.encoding = static_cast<ArenaSlabEncoding>(encoding);
    if (image.encoding != ArenaSlabEncoding::RawBytes && image.encoding != ArenaSlabEncoding::Structured) {
        return false;
    }
    std::memcpy(&image.slabIndex, cursor, sizeof(uint16_t));
    cursor += sizeof(uint16_t);
    std::memcpy(&image.count, cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);

    size_t inUseSize = 0;
    std::memcpy(&inUseSize, cursor, sizeof(size_t));
    cursor += sizeof(size_t);
    const size_t expected = sizeof(uint8_t) + sizeof(uint16_t) + sizeof(uint32_t) + sizeof(size_t) + inUseSize * sizeof(size_t);
    if (text.size() < expected) {
        return false;
    }

    image.inUse.resize(inUseSize);
    if (inUseSize > 0) {
        std::memcpy(image.inUse.data(), cursor, inUseSize * sizeof(size_t));
        cursor += inUseSize * sizeof(size_t);
    }

    if (image.encoding == ArenaSlabEncoding::RawBytes) {
        const size_t remaining = text.data() + text.size() - cursor;
        image.bytes.resize(remaining);
        if (remaining > 0) {
            std::memcpy(image.bytes.data(), cursor, remaining);
        }
        return true;
    }

    if (static_cast<size_t>(text.data() + text.size() - cursor) < sizeof(size_t)) {
        return false;
    }
    size_t slotCount = 0;
    std::memcpy(&slotCount, cursor, sizeof(size_t));
    cursor += sizeof(size_t);
    image.structuredSlots.clear();
    image.structuredSlots.reserve(slotCount);
    for (size_t i = 0; i < slotCount; ++i) {
        if (static_cast<size_t>(text.data() + text.size() - cursor) < sizeof(uint16_t) + sizeof(size_t)) {
            return false;
        }
        ArenaStructuredSlot slot;
        std::memcpy(&slot.offset, cursor, sizeof(uint16_t));
        cursor += sizeof(uint16_t);
        size_t payloadSize = 0;
        std::memcpy(&payloadSize, cursor, sizeof(size_t));
        cursor += sizeof(size_t);
        if (static_cast<size_t>(text.data() + text.size() - cursor) < payloadSize) {
            return false;
        }
        slot.payload.assign(cursor, payloadSize);
        cursor += payloadSize;
        image.structuredSlots.push_back(std::move(slot));
    }
    return cursor == text.data() + text.size();
}

std::string encodeRootState(const ArenaRootState& rootState) {
    std::string out;
    arena_persistence_detail::appendPod(out, rootState.version);
    size_t anchorCount = rootState.anchors.size();
    arena_persistence_detail::appendPod(out, anchorCount);
    for (const auto& anchor : rootState.anchors) {
        arena_persistence_detail::appendString(out, anchor.name);
        arena_persistence_detail::appendSlabId(out, anchor.valueSid);
    }
    return out;
}

bool decodeRootState(const std::string& text, ArenaRootState& rootState) {
    rootState = {};
    size_t cursor = 0;
    size_t anchorCount = 0;
    if (!arena_persistence_detail::readPod(text, cursor, rootState.version) ||
        !arena_persistence_detail::readPod(text, cursor, anchorCount)) {
        return false;
    }
    rootState.anchors.clear();
    rootState.anchors.reserve(anchorCount);
    for (size_t i = 0; i < anchorCount; ++i) {
        ArenaRootAnchor anchor;
        if (!arena_persistence_detail::readString(text, cursor, anchor.name) ||
            !arena_persistence_detail::readSlabId(text, cursor, anchor.valueSid)) {
            return false;
        }
        rootState.anchors.push_back(std::move(anchor));
    }
    return cursor == text.size();
}

} // namespace

bool MemoryArenaStore::loadCurrent(ArenaCheckpointMetadata& out) {
    if (!hasCurrent) {
        return false;
    }
    out = current;
    return true;
}

bool MemoryArenaStore::saveCurrent(const ArenaCheckpointMetadata& checkpoint) {
    current = checkpoint;
    hasCurrent = true;
    return true;
}

bool MemoryArenaStore::saveNamedCheckpoint(const std::string& name, const ArenaCheckpointMetadata& checkpoint) {
    named[name] = checkpoint;
    return true;
}

bool MemoryArenaStore::loadNamedCheckpoint(const std::string& name, ArenaCheckpointMetadata& out) {
    auto it = named.find(name);
    if (it == named.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool MemoryArenaStore::saveSlab(const ArenaSlabImage& slab) {
    slabs[slab.slabIndex] = slab;
    return true;
}

bool MemoryArenaStore::loadSlab(uint16_t slabIndex, ArenaSlabImage& out) {
    auto it = slabs.find(slabIndex);
    if (it == slabs.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool MemoryArenaStore::saveRootState(const std::string& name, const ArenaRootState& rootState) {
    rootStates[name] = rootState;
    return true;
}

bool MemoryArenaStore::loadRootState(const std::string& name, ArenaRootState& out) {
    auto it = rootStates.find(name);
    if (it == rootStates.end()) {
        return false;
    }
    out = it->second;
    return true;
}

FileArenaStore::FileArenaStore(std::string filePath) : path(std::move(filePath)) {}

bool FileArenaStore::loadCurrent(ArenaCheckpointMetadata& out) {
    bool hasCurrentValue = false;
    ArenaCheckpointMetadata currentValue;
    std::map<std::string, ArenaCheckpointMetadata> namedValue;
    if (!readAll(hasCurrentValue, currentValue, namedValue) || !hasCurrentValue) {
        return false;
    }
    out = currentValue;
    return true;
}

bool FileArenaStore::saveCurrent(const ArenaCheckpointMetadata& checkpoint) {
    bool hasCurrentValue = false;
    ArenaCheckpointMetadata currentValue;
    std::map<std::string, ArenaCheckpointMetadata> namedValue;
    if (!readAll(hasCurrentValue, currentValue, namedValue)) {
        return false;
    }
    currentValue = checkpoint;
    return writeAll(true, currentValue, namedValue);
}

bool FileArenaStore::saveNamedCheckpoint(const std::string& name, const ArenaCheckpointMetadata& checkpoint) {
    bool hasCurrentValue = false;
    ArenaCheckpointMetadata currentValue;
    std::map<std::string, ArenaCheckpointMetadata> namedValue;
    if (!readAll(hasCurrentValue, currentValue, namedValue)) {
        return false;
    }
    namedValue[name] = checkpoint;
    return writeAll(hasCurrentValue, currentValue, namedValue);
}

bool FileArenaStore::loadNamedCheckpoint(const std::string& name, ArenaCheckpointMetadata& out) {
    bool hasCurrentValue = false;
    ArenaCheckpointMetadata currentValue;
    std::map<std::string, ArenaCheckpointMetadata> namedValue;
    if (!readAll(hasCurrentValue, currentValue, namedValue)) {
        return false;
    }
    auto it = namedValue.find(name);
    if (it == namedValue.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool FileArenaStore::saveSlab(const ArenaSlabImage& slab) {
    std::ofstream out(path + ".slab." + std::to_string(slab.slabIndex), std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        return false;
    }
    auto encoded = encodeSlabImage(slab);
    out.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    return out.good();
}

bool FileArenaStore::loadSlab(uint16_t slabIndex, ArenaSlabImage& out) {
    std::ifstream in(path + ".slab." + std::to_string(slabIndex), std::ios::binary);
    if (!in.good()) {
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decodeSlabImage(data, out);
}

bool FileArenaStore::saveRootState(const std::string& name, const ArenaRootState& rootState) {
    std::ofstream out(path + ".root." + name, std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        return false;
    }
    auto encoded = encodeRootState(rootState);
    out.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    return out.good();
}

bool FileArenaStore::loadRootState(const std::string& name, ArenaRootState& out) {
    std::ifstream in(path + ".root." + name, std::ios::binary);
    if (!in.good()) {
        return false;
    }
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decodeRootState(data, out);
}

bool FileArenaStore::readAll(bool& hasCurrentOut,
                             ArenaCheckpointMetadata& currentOut,
                             std::map<std::string, ArenaCheckpointMetadata>& namedOut) const {
    hasCurrentOut = false;
    currentOut = {};
    namedOut.clear();

    std::ifstream in(path);
    if (!in.good()) {
        return true;
    }

    std::string kind;
    while (in >> kind) {
        if (kind == "current") {
            std::string payload;
            std::getline(in >> std::ws, payload);
            if (!decodeMetadata(payload, currentOut)) {
                return false;
            }
            hasCurrentOut = true;
            continue;
        }

        if (kind == "named") {
            std::string name;
            if (!(in >> name)) {
                return false;
            }
            std::string payload;
            std::getline(in >> std::ws, payload);
            ArenaCheckpointMetadata metadata;
            if (!decodeMetadata(payload, metadata)) {
                return false;
            }
            namedOut[name] = metadata;
            continue;
        }

        return false;
    }

    return true;
}

bool FileArenaStore::writeAll(bool hasCurrentValue,
                              const ArenaCheckpointMetadata& currentValue,
                              const std::map<std::string, ArenaCheckpointMetadata>& namedValue) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.good()) {
        return false;
    }

    if (hasCurrentValue) {
        out << "current " << encodeMetadata(currentValue) << '\n';
    }

    for (const auto& entry : namedValue) {
        out << "named " << entry.first << ' ' << encodeMetadata(entry.second) << '\n';
    }

    return out.good();
}

class LmdbArenaStore::Impl {
public:
    struct MDB_val {
        size_t mv_size;
        void* mv_data;
    };

    using MDB_dbi = unsigned int;
    using mdb_mode_t = unsigned int;
    struct MDB_env;
    struct MDB_txn;

    using EnvCreateFn = int (*)(MDB_env**);
    using EnvSetMaxdbsFn = int (*)(MDB_env*, MDB_dbi);
    using EnvSetMapsFn = int (*)(MDB_env*, size_t);
    using EnvOpenFn = int (*)(MDB_env*, const char*, unsigned int, mdb_mode_t);
    using EnvCloseFn = void (*)(MDB_env*);
    using TxnBeginFn = int (*)(MDB_env*, MDB_txn*, unsigned int, MDB_txn**);
    using TxnCommitFn = int (*)(MDB_txn*);
    using TxnAbortFn = void (*)(MDB_txn*);
    using DbiOpenFn = int (*)(MDB_txn*, const char*, unsigned int, MDB_dbi*);
    using DbiCloseFn = void (*)(MDB_env*, MDB_dbi);
    using GetFn = int (*)(MDB_txn*, MDB_dbi, MDB_val*, MDB_val*);
    using PutFn = int (*)(MDB_txn*, MDB_dbi, MDB_val*, MDB_val*, unsigned int);

    static constexpr unsigned int MDB_CREATE = 0x40000;

    std::string directoryPath;
    void* handle = nullptr;
    MDB_env* env = nullptr;
    MDB_dbi dbi = 0;
    bool ready = false;

    EnvCreateFn envCreate = nullptr;
    EnvSetMaxdbsFn envSetMaxdbs = nullptr;
    EnvSetMapsFn envSetMaps = nullptr;
    EnvOpenFn envOpen = nullptr;
    EnvCloseFn envClose = nullptr;
    TxnBeginFn txnBegin = nullptr;
    TxnCommitFn txnCommit = nullptr;
    TxnAbortFn txnAbort = nullptr;
    DbiOpenFn dbiOpen = nullptr;
    DbiCloseFn dbiClose = nullptr;
    GetFn get = nullptr;
    PutFn put = nullptr;

    explicit Impl(std::string path) : directoryPath(std::move(path)) {}

    ~Impl() {
        if (env && dbiClose) {
            dbiClose(env, dbi);
        }
        if (env && envClose) {
            envClose(env);
        }
        if (handle) {
            dlclose(handle);
        }
    }

    bool init() {
        if (ready) {
            return true;
        }

        if (!loadSymbols()) {
            return false;
        }

        std::filesystem::create_directories(directoryPath);
        if (envCreate(&env) != 0) {
            return false;
        }
        if (envSetMaxdbs(env, 1) != 0) {
            return false;
        }
        if (envSetMaps(env, 1u << 20) != 0) {
            return false;
        }
        if (envOpen(env, directoryPath.c_str(), 0, 0664) != 0) {
            return false;
        }

        MDB_txn* txn = nullptr;
        if (txnBegin(env, nullptr, 0, &txn) != 0) {
            return false;
        }
        if (dbiOpen(txn, nullptr, MDB_CREATE, &dbi) != 0) {
            txnAbort(txn);
            return false;
        }
        if (txnCommit(txn) != 0) {
            return false;
        }

        ready = true;
        return true;
    }

    bool loadSymbols() {
        if (handle) {
            return true;
        }

        for (const char* soname : {"liblmdb.so.0.0.0", "liblmdb.so.0", "liblmdb.so"}) {
            handle = dlopen(soname, RTLD_NOW | RTLD_LOCAL);
            if (handle) {
                break;
            }
        }
        if (!handle) {
            return false;
        }

        envCreate = reinterpret_cast<EnvCreateFn>(dlsym(handle, "mdb_env_create"));
        envSetMaxdbs = reinterpret_cast<EnvSetMaxdbsFn>(dlsym(handle, "mdb_env_set_maxdbs"));
        envSetMaps = reinterpret_cast<EnvSetMapsFn>(dlsym(handle, "mdb_env_set_mapsize"));
        envOpen = reinterpret_cast<EnvOpenFn>(dlsym(handle, "mdb_env_open"));
        envClose = reinterpret_cast<EnvCloseFn>(dlsym(handle, "mdb_env_close"));
        txnBegin = reinterpret_cast<TxnBeginFn>(dlsym(handle, "mdb_txn_begin"));
        txnCommit = reinterpret_cast<TxnCommitFn>(dlsym(handle, "mdb_txn_commit"));
        txnAbort = reinterpret_cast<TxnAbortFn>(dlsym(handle, "mdb_txn_abort"));
        dbiOpen = reinterpret_cast<DbiOpenFn>(dlsym(handle, "mdb_dbi_open"));
        dbiClose = reinterpret_cast<DbiCloseFn>(dlsym(handle, "mdb_dbi_close"));
        get = reinterpret_cast<GetFn>(dlsym(handle, "mdb_get"));
        put = reinterpret_cast<PutFn>(dlsym(handle, "mdb_put"));

        return envCreate && envSetMaxdbs && envSetMaps && envOpen && envClose && txnBegin &&
               txnCommit && txnAbort && dbiOpen && dbiClose && get && put;
    }

    bool storeValue(const std::string& key, const std::string& value) {
        if (!init()) {
            return false;
        }

        MDB_txn* txn = nullptr;
        if (txnBegin(env, nullptr, 0, &txn) != 0) {
            return false;
        }

        MDB_val keyVal{key.size(), const_cast<char*>(key.data())};
        MDB_val valueVal{value.size(), const_cast<char*>(value.data())};
        if (put(txn, dbi, &keyVal, &valueVal, 0) != 0) {
            txnAbort(txn);
            return false;
        }
        return txnCommit(txn) == 0;
    }

    bool loadValue(const std::string& key, std::string& value) {
        if (!init()) {
            return false;
        }

        MDB_txn* txn = nullptr;
        if (txnBegin(env, nullptr, 0, &txn) != 0) {
            return false;
        }

        MDB_val keyVal{key.size(), const_cast<char*>(key.data())};
        MDB_val valueVal{};
        int rc = get(txn, dbi, &keyVal, &valueVal);
        if (rc != 0) {
            txnAbort(txn);
            return false;
        }

        value.assign(static_cast<const char*>(valueVal.mv_data), valueVal.mv_size);
        txnAbort(txn);
        return true;
    }
};

LmdbArenaStore::LmdbArenaStore(std::string directoryPath)
    : impl(std::make_unique<Impl>(std::move(directoryPath))) {}

LmdbArenaStore::~LmdbArenaStore() = default;

bool LmdbArenaStore::loadCurrent(ArenaCheckpointMetadata& out) {
    std::string value;
    if (!impl->loadValue("meta/current", value)) {
        return false;
    }
    return decodeMetadata(value, out);
}

bool LmdbArenaStore::saveCurrent(const ArenaCheckpointMetadata& checkpoint) {
    return impl->storeValue("meta/current", encodeMetadata(checkpoint));
}

bool LmdbArenaStore::saveNamedCheckpoint(const std::string& name, const ArenaCheckpointMetadata& checkpoint) {
    return impl->storeValue("checkpoint/" + name, encodeMetadata(checkpoint));
}

bool LmdbArenaStore::loadNamedCheckpoint(const std::string& name, ArenaCheckpointMetadata& out) {
    std::string value;
    if (!impl->loadValue("checkpoint/" + name, value)) {
        return false;
    }
    return decodeMetadata(value, out);
}

bool LmdbArenaStore::saveSlab(const ArenaSlabImage& slab) {
    return impl->storeValue("slab/" + std::to_string(slab.slabIndex), encodeSlabImage(slab));
}

bool LmdbArenaStore::loadSlab(uint16_t slabIndex, ArenaSlabImage& out) {
    std::string value;
    if (!impl->loadValue("slab/" + std::to_string(slabIndex), value)) {
        return false;
    }
    return decodeSlabImage(value, out);
}

bool LmdbArenaStore::saveRootState(const std::string& name, const ArenaRootState& rootState) {
    return impl->storeValue("root/" + name, encodeRootState(rootState));
}

bool LmdbArenaStore::loadRootState(const std::string& name, ArenaRootState& out) {
    std::string value;
    if (!impl->loadValue("root/" + name, value)) {
        return false;
    }
    return decodeRootState(value, out);
}

bool LmdbArenaStore::isAvailable() const {
    return const_cast<Impl*>(impl.get())->loadSymbols();
}
