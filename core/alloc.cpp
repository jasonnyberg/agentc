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

