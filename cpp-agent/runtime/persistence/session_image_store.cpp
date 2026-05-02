#include "session_image_store.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace agentc::runtime {
namespace {

std::string sanitize_session_name(std::string value) {
    if (value.empty()) {
        return "default";
    }
    std::replace_if(value.begin(), value.end(), [](unsigned char ch) {
        return !(std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.');
    }, '_');
    return value.empty() ? "default" : value;
}

constexpr const char* kSessionImageMmapSlabFormat = "session_image_mmap_v1";
constexpr uint32_t kSessionImageMmapSlabVersion = 1;
constexpr const char* kSessionImageMmapRawSlabFormat = "session_image_mmap_raw_v1";
constexpr uint32_t kSessionImageMmapRawSlabVersion = 1;
constexpr const char* kSessionImageMmapStructuredAttachSlabFormat = "session_image_mmap_structured_attach_v1";
constexpr uint32_t kSessionImageMmapStructuredAttachSlabVersion = 1;

struct SessionImageMmapSlabHeader {
    char magic[8];
    uint32_t version = kSessionImageMmapSlabVersion;
    uint32_t payload_offset_bytes = 0;
    uint32_t payload_size_bytes = 0;
    uint16_t slab_index = 0;
    uint8_t encoding = 0;
    uint8_t reserved = 0;
};

struct SessionImageMmapRawSlabHeader {
    char magic[8];
    uint32_t version = kSessionImageMmapRawSlabVersion;
    uint32_t in_use_offset_bytes = 0;
    uint32_t in_use_entry_count = 0;
    uint32_t items_offset_bytes = 0;
    uint32_t items_size_bytes = 0;
    uint32_t count = 0;
    uint32_t item_size_bytes = 0;
    uint16_t slab_index = 0;
    uint8_t encoding = 0;
    uint8_t reserved = 0;
};

struct SessionImageMmapStructuredAttachSlabHeader {
    char magic[8];
    uint32_t version = kSessionImageMmapStructuredAttachSlabVersion;
    uint32_t payload_offset_bytes = 0;
    uint32_t payload_size_bytes = 0;
    uint32_t items_offset_bytes = 0;
    uint32_t items_size_bytes = 0;
    uint16_t slab_index = 0;
    uint8_t encoding = 0;
    uint8_t reserved = 0;
};

size_t systemPageSize() {
    const long value = ::sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<size_t>(value) : static_cast<size_t>(4096);
}

size_t alignUp(size_t value, size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

std::array<char, 8> slabMagicBytes() {
    return {'A', 'S', 'L', 'A', 'B', 'V', '1', '\0'};
}

std::array<char, 8> rawSlabMagicBytes() {
    return {'A', 'R', 'A', 'W', 'S', 'V', '1', '\0'};
}

std::array<char, 8> structuredAttachSlabMagicBytes() {
    return {'A', 'S', 'T', 'R', 'V', '1', '\0', '\0'};
}

bool hasExpectedSlabMagic(const SessionImageMmapSlabHeader& header) {
    const auto magic = slabMagicBytes();
    return std::memcmp(header.magic, magic.data(), magic.size()) == 0;
}

bool hasExpectedRawSlabMagic(const SessionImageMmapRawSlabHeader& header) {
    const auto magic = rawSlabMagicBytes();
    return std::memcmp(header.magic, magic.data(), magic.size()) == 0;
}

bool hasExpectedStructuredAttachSlabMagic(const SessionImageMmapStructuredAttachSlabHeader& header) {
    const auto magic = structuredAttachSlabMagicBytes();
    return std::memcmp(header.magic, magic.data(), magic.size()) == 0;
}

std::string encodeMmapFriendlySlabFile(const ArenaSlabImage& slab,
                                       SessionImageSlabFile& slab_file) {
    const std::string payload = serializeArenaSlabImage(slab);
    SessionImageMmapSlabHeader header;
    const auto magic = slabMagicBytes();
    std::memcpy(header.magic, magic.data(), magic.size());
    header.version = kSessionImageMmapSlabVersion;
    header.payload_offset_bytes = static_cast<uint32_t>(alignUp(sizeof(SessionImageMmapSlabHeader), systemPageSize()));
    header.payload_size_bytes = static_cast<uint32_t>(payload.size());
    header.slab_index = slab.slabIndex;
    header.encoding = static_cast<uint8_t>(slab.encoding);

    std::string file_bytes(header.payload_offset_bytes + payload.size(), '\0');
    std::memcpy(file_bytes.data(), &header, sizeof(header));
    if (!payload.empty()) {
        std::memcpy(file_bytes.data() + header.payload_offset_bytes, payload.data(), payload.size());
    }

    slab_file.format = kSessionImageMmapSlabFormat;
    slab_file.payload_offset_bytes = header.payload_offset_bytes;
    slab_file.payload_size_bytes = header.payload_size_bytes;
    return file_bytes;
}

std::string encodeMmapAttachableRawSlabFile(const ArenaSlabImage& slab,
                                            SessionImageSlabFile& slab_file) {
    SessionImageMmapRawSlabHeader header;
    const auto magic = rawSlabMagicBytes();
    std::memcpy(header.magic, magic.data(), magic.size());
    header.version = kSessionImageMmapRawSlabVersion;
    header.slab_index = slab.slabIndex;
    header.encoding = static_cast<uint8_t>(slab.encoding);
    header.count = slab.count;
    header.in_use_entry_count = static_cast<uint32_t>(slab.inUse.size());
    header.item_size_bytes = slab.inUse.empty() ? 0u : static_cast<uint32_t>(slab.bytes.size() / slab.inUse.size());
    header.in_use_offset_bytes = static_cast<uint32_t>(alignUp(sizeof(SessionImageMmapRawSlabHeader), alignof(size_t)));
    header.items_offset_bytes = static_cast<uint32_t>(alignUp(header.in_use_offset_bytes + slab.inUse.size() * sizeof(size_t), systemPageSize()));
    header.items_size_bytes = static_cast<uint32_t>(slab.bytes.size());

    std::string file_bytes(header.items_offset_bytes + slab.bytes.size(), '\0');
    std::memcpy(file_bytes.data(), &header, sizeof(header));
    if (!slab.inUse.empty()) {
        std::memcpy(file_bytes.data() + header.in_use_offset_bytes,
                    slab.inUse.data(),
                    slab.inUse.size() * sizeof(size_t));
    }
    if (!slab.bytes.empty()) {
        std::memcpy(file_bytes.data() + header.items_offset_bytes,
                    slab.bytes.data(),
                    slab.bytes.size());
    }

    slab_file.format = kSessionImageMmapRawSlabFormat;
    slab_file.payload_offset_bytes = header.items_offset_bytes;
    slab_file.payload_size_bytes = header.items_size_bytes;
    return file_bytes;
}

std::string encodeMmapAttachableStructuredSlabFile(const ArenaSlabImage& slab,
                                                   size_t item_size_bytes,
                                                   SessionImageSlabFile& slab_file) {
    const std::string payload = serializeArenaSlabImage(slab);
    SessionImageMmapStructuredAttachSlabHeader header;
    const auto magic = structuredAttachSlabMagicBytes();
    std::memcpy(header.magic, magic.data(), magic.size());
    header.version = kSessionImageMmapStructuredAttachSlabVersion;
    header.payload_offset_bytes = static_cast<uint32_t>(alignUp(sizeof(SessionImageMmapStructuredAttachSlabHeader), systemPageSize()));
    header.payload_size_bytes = static_cast<uint32_t>(payload.size());
    header.items_offset_bytes = static_cast<uint32_t>(alignUp(header.payload_offset_bytes + payload.size(), systemPageSize()));
    header.items_size_bytes = static_cast<uint32_t>(SLAB_SIZE * item_size_bytes);
    header.slab_index = slab.slabIndex;
    header.encoding = static_cast<uint8_t>(slab.encoding);

    std::string file_bytes(header.items_offset_bytes + header.items_size_bytes, '\0');
    std::memcpy(file_bytes.data(), &header, sizeof(header));
    if (!payload.empty()) {
        std::memcpy(file_bytes.data() + header.payload_offset_bytes, payload.data(), payload.size());
    }

    slab_file.format = kSessionImageMmapStructuredAttachSlabFormat;
    slab_file.payload_offset_bytes = header.payload_offset_bytes;
    slab_file.payload_size_bytes = header.payload_size_bytes;
    return file_bytes;
}

bool decodeMmapFriendlySlabPayload(const void* mapped_data,
                                   size_t mapped_size,
                                   const SessionImageSlabFile& slab_file,
                                   ArenaSlabImage& slab,
                                   std::string* error) {
    if (mapped_size < sizeof(SessionImageMmapSlabHeader)) {
        if (error) *error = "mmap slab file is smaller than header";
        return false;
    }

    SessionImageMmapSlabHeader header;
    std::memcpy(&header, mapped_data, sizeof(header));
    if (!hasExpectedSlabMagic(header) || header.version != kSessionImageMmapSlabVersion) {
        if (error) *error = "mmap slab file has invalid header";
        return false;
    }
    if (header.slab_index != slab_file.index) {
        if (error) *error = "mmap slab file index does not match manifest";
        return false;
    }
    if (header.payload_offset_bytes > mapped_size ||
        header.payload_size_bytes > mapped_size - header.payload_offset_bytes) {
        if (error) *error = "mmap slab file payload range is invalid";
        return false;
    }
    if (slab_file.payload_offset_bytes != 0 && slab_file.payload_offset_bytes != header.payload_offset_bytes) {
        if (error) *error = "mmap slab manifest payload offset does not match header";
        return false;
    }
    if (slab_file.payload_size_bytes != 0 && slab_file.payload_size_bytes != header.payload_size_bytes) {
        if (error) *error = "mmap slab manifest payload size does not match header";
        return false;
    }

    const auto* payload = static_cast<const char*>(mapped_data) + header.payload_offset_bytes;
    return deserializeArenaSlabImage(std::string_view(payload, header.payload_size_bytes), slab);
}

bool decodeMmapAttachableRawSlab(const void* mapped_data,
                                 size_t mapped_size,
                                 const SessionImageSlabFile& slab_file,
                                 ArenaSlabImage& slab,
                                 std::string* error) {
    if (mapped_size < sizeof(SessionImageMmapRawSlabHeader)) {
        if (error) *error = "raw mmap slab file is smaller than header";
        return false;
    }

    SessionImageMmapRawSlabHeader header;
    std::memcpy(&header, mapped_data, sizeof(header));
    if (!hasExpectedRawSlabMagic(header) || header.version != kSessionImageMmapRawSlabVersion) {
        if (error) *error = "raw mmap slab file has invalid header";
        return false;
    }
    if (header.encoding != static_cast<uint8_t>(ArenaSlabEncoding::RawBytes)) {
        if (error) *error = "raw mmap slab file does not declare raw-byte encoding";
        return false;
    }
    if (header.slab_index != slab_file.index) {
        if (error) *error = "raw mmap slab file index does not match manifest";
        return false;
    }
    if (header.in_use_offset_bytes > mapped_size ||
        header.in_use_entry_count > (mapped_size - header.in_use_offset_bytes) / sizeof(size_t) ||
        header.items_offset_bytes > mapped_size ||
        header.items_size_bytes > mapped_size - header.items_offset_bytes) {
        if (error) *error = "raw mmap slab file ranges are invalid";
        return false;
    }
    if (slab_file.payload_offset_bytes != 0 && slab_file.payload_offset_bytes != header.items_offset_bytes) {
        if (error) *error = "raw mmap slab manifest payload offset does not match header";
        return false;
    }
    if (slab_file.payload_size_bytes != 0 && slab_file.payload_size_bytes != header.items_size_bytes) {
        if (error) *error = "raw mmap slab manifest payload size does not match header";
        return false;
    }

    slab = {};
    slab.slabIndex = header.slab_index;
    slab.count = header.count;
    slab.encoding = ArenaSlabEncoding::RawBytes;
    slab.inUse.resize(header.in_use_entry_count);
    if (header.in_use_entry_count > 0) {
        std::memcpy(slab.inUse.data(),
                    static_cast<const char*>(mapped_data) + header.in_use_offset_bytes,
                    header.in_use_entry_count * sizeof(size_t));
    }
    slab.bytes.resize(header.items_size_bytes);
    if (header.items_size_bytes > 0) {
        std::memcpy(slab.bytes.data(),
                    static_cast<const char*>(mapped_data) + header.items_offset_bytes,
                    header.items_size_bytes);
    }
    return true;
}

bool decodeMmapAttachableStructuredSlab(const void* mapped_data,
                                        size_t mapped_size,
                                        const SessionImageSlabFile& slab_file,
                                        ArenaSlabImage& slab,
                                        std::string* error) {
    if (mapped_size < sizeof(SessionImageMmapStructuredAttachSlabHeader)) {
        if (error) *error = "structured attach mmap slab file is smaller than header";
        return false;
    }

    SessionImageMmapStructuredAttachSlabHeader header;
    std::memcpy(&header, mapped_data, sizeof(header));
    if (!hasExpectedStructuredAttachSlabMagic(header) || header.version != kSessionImageMmapStructuredAttachSlabVersion) {
        if (error) *error = "structured attach mmap slab file has invalid header";
        return false;
    }
    if (header.encoding != static_cast<uint8_t>(ArenaSlabEncoding::Structured)) {
        if (error) *error = "structured attach mmap slab file does not declare structured encoding";
        return false;
    }
    if (header.slab_index != slab_file.index) {
        if (error) *error = "structured attach mmap slab file index does not match manifest";
        return false;
    }
    if (header.payload_offset_bytes > mapped_size ||
        header.payload_size_bytes > mapped_size - header.payload_offset_bytes ||
        header.items_offset_bytes > mapped_size ||
        header.items_size_bytes > mapped_size - header.items_offset_bytes) {
        if (error) *error = "structured attach mmap slab file ranges are invalid";
        return false;
    }
    if (slab_file.payload_offset_bytes != 0 && slab_file.payload_offset_bytes != header.payload_offset_bytes) {
        if (error) *error = "structured attach mmap slab manifest payload offset does not match header";
        return false;
    }
    if (slab_file.payload_size_bytes != 0 && slab_file.payload_size_bytes != header.payload_size_bytes) {
        if (error) *error = "structured attach mmap slab manifest payload size does not match header";
        return false;
    }

    const auto* payload = static_cast<const char*>(mapped_data) + header.payload_offset_bytes;
    if (!deserializeArenaSlabImage(std::string_view(payload, header.payload_size_bytes), slab)) {
        if (error) *error = "failed to decode structured attach slab payload";
        return false;
    }
    if (slab.encoding != ArenaSlabEncoding::Structured || slab.slabIndex != slab_file.index) {
        if (error) *error = "decoded structured attach slab payload is inconsistent with header";
        return false;
    }
    return true;
}

bool mapMmapAttachableStructuredSlab(const void* mapped_data,
                                     size_t mapped_size,
                                     const SessionImageSlabFile& slab_file,
                                     size_t expected_item_size_bytes,
                                     const std::shared_ptr<void>& mapped_region,
                                     ArenaMappedStructuredSlabAttachment& attachment,
                                     std::string* error) {
    if (mapped_size < sizeof(SessionImageMmapStructuredAttachSlabHeader)) {
        if (error) *error = "structured attach mmap slab file is smaller than header";
        return false;
    }

    SessionImageMmapStructuredAttachSlabHeader header;
    std::memcpy(&header, mapped_data, sizeof(header));
    if (!hasExpectedStructuredAttachSlabMagic(header) || header.version != kSessionImageMmapStructuredAttachSlabVersion) {
        if (error) *error = "structured attach mmap slab file has invalid header";
        return false;
    }
    if (header.encoding != static_cast<uint8_t>(ArenaSlabEncoding::Structured)) {
        if (error) *error = "structured attach mmap slab file does not declare structured encoding";
        return false;
    }
    if (header.slab_index != slab_file.index) {
        if (error) *error = "structured attach mmap slab file index does not match manifest";
        return false;
    }
    if (header.items_size_bytes != expected_item_size_bytes * static_cast<uint64_t>(SLAB_SIZE) ||
        header.payload_offset_bytes > mapped_size ||
        header.payload_size_bytes > mapped_size - header.payload_offset_bytes ||
        header.items_offset_bytes > mapped_size ||
        header.items_size_bytes > mapped_size - header.items_offset_bytes) {
        if (error) *error = "structured attach mmap slab file ranges are invalid";
        return false;
    }
    if (slab_file.payload_offset_bytes != 0 && slab_file.payload_offset_bytes != header.payload_offset_bytes) {
        if (error) *error = "structured attach mmap slab manifest payload offset does not match header";
        return false;
    }
    if (slab_file.payload_size_bytes != 0 && slab_file.payload_size_bytes != header.payload_size_bytes) {
        if (error) *error = "structured attach mmap slab manifest payload size does not match header";
        return false;
    }

    ArenaSlabImage slab;
    const auto* payload = static_cast<const char*>(mapped_data) + header.payload_offset_bytes;
    if (!deserializeArenaSlabImage(std::string_view(payload, header.payload_size_bytes), slab)) {
        if (error) *error = "failed to decode structured attach slab payload";
        return false;
    }
    if (slab.encoding != ArenaSlabEncoding::Structured || slab.slabIndex != slab_file.index || slab.inUse.size() != SLAB_SIZE) {
        if (error) *error = "decoded structured attach slab payload is inconsistent";
        return false;
    }

    attachment = {};
    attachment.slabIndex = slab.slabIndex;
    attachment.count = slab.count;
    attachment.inUse = std::move(slab.inUse);
    attachment.structuredSlots = std::move(slab.structuredSlots);
    attachment.mappedRegion = mapped_region;
    attachment.mappedRegionSize = mapped_size;
    attachment.items = reinterpret_cast<std::byte*>(static_cast<char*>(mapped_region.get()) + header.items_offset_bytes);
    attachment.itemsSizeBytes = header.items_size_bytes;
    return true;
}

bool mapMmapAttachableRawSlab(const void* mapped_data,
                              size_t mapped_size,
                              const SessionImageSlabFile& slab_file,
                              size_t expected_item_size_bytes,
                              const std::shared_ptr<void>& mapped_region,
                              ArenaMappedRawSlabAttachment& slab,
                              std::string* error) {
    if (mapped_size < sizeof(SessionImageMmapRawSlabHeader)) {
        if (error) *error = "raw mmap slab file is smaller than header";
        return false;
    }

    SessionImageMmapRawSlabHeader header;
    std::memcpy(&header, mapped_data, sizeof(header));
    if (!hasExpectedRawSlabMagic(header) || header.version != kSessionImageMmapRawSlabVersion) {
        if (error) *error = "raw mmap slab file has invalid header";
        return false;
    }
    if (header.encoding != static_cast<uint8_t>(ArenaSlabEncoding::RawBytes)) {
        if (error) *error = "raw mmap slab file does not declare raw-byte encoding";
        return false;
    }
    if (header.slab_index != slab_file.index) {
        if (error) *error = "raw mmap slab file index does not match manifest";
        return false;
    }
    if (header.in_use_entry_count != SLAB_SIZE || header.item_size_bytes != expected_item_size_bytes) {
        if (error) *error = "raw mmap slab file does not match allocator item geometry";
        return false;
    }
    if (header.in_use_offset_bytes > mapped_size ||
        header.in_use_entry_count > (mapped_size - header.in_use_offset_bytes) / sizeof(size_t) ||
        header.items_offset_bytes > mapped_size ||
        header.items_size_bytes != expected_item_size_bytes * static_cast<uint64_t>(SLAB_SIZE) ||
        header.items_size_bytes > mapped_size - header.items_offset_bytes) {
        if (error) *error = "raw mmap slab file ranges are invalid";
        return false;
    }
    if (slab_file.payload_offset_bytes != 0 && slab_file.payload_offset_bytes != header.items_offset_bytes) {
        if (error) *error = "raw mmap slab manifest payload offset does not match header";
        return false;
    }
    if (slab_file.payload_size_bytes != 0 && slab_file.payload_size_bytes != header.items_size_bytes) {
        if (error) *error = "raw mmap slab manifest payload size does not match header";
        return false;
    }

    slab = {};
    slab.slabIndex = header.slab_index;
    slab.count = header.count;
    slab.inUse.resize(header.in_use_entry_count);
    std::memcpy(slab.inUse.data(),
                static_cast<const char*>(mapped_data) + header.in_use_offset_bytes,
                header.in_use_entry_count * sizeof(size_t));
    slab.mappedRegion = mapped_region;
    slab.mappedRegionSize = mapped_size;
    slab.items = reinterpret_cast<std::byte*>(static_cast<char*>(mapped_region.get()) + header.items_offset_bytes);
    slab.itemsSizeBytes = header.items_size_bytes;
    return true;
}

nlohmann::json slabFileToJson(const SessionImageSlabFile& slab) {
    return nlohmann::json{
        {"index", slab.index},
        {"file", slab.file},
        {"format", slab.format},
        {"payload_offset_bytes", slab.payload_offset_bytes},
        {"payload_size_bytes", slab.payload_size_bytes},
    };
}

bool slabFileFromJson(const nlohmann::json& json, SessionImageSlabFile& slab) {
    if (!json.is_object() || !json.contains("index") || !json.contains("file") ||
        !json["index"].is_number_unsigned() || !json["file"].is_string()) {
        return false;
    }
    slab = {};
    slab.index = json["index"].get<uint16_t>();
    slab.file = json["file"].get<std::string>();
    if (json.contains("format")) {
        if (!json["format"].is_string()) {
            return false;
        }
        slab.format = json["format"].get<std::string>();
    }
    if (json.contains("payload_offset_bytes")) {
        if (!json["payload_offset_bytes"].is_number_unsigned()) {
            return false;
        }
        slab.payload_offset_bytes = json["payload_offset_bytes"].get<uint64_t>();
    }
    if (json.contains("payload_size_bytes")) {
        if (!json["payload_size_bytes"].is_number_unsigned()) {
            return false;
        }
        slab.payload_size_bytes = json["payload_size_bytes"].get<uint64_t>();
    }
    return !slab.file.empty();
}

nlohmann::json allocatorToJson(const SessionImageAllocatorManifest& allocator) {
    nlohmann::json slabs = nlohmann::json::array();
    for (const auto& slab : allocator.slabs) {
        slabs.push_back(slabFileToJson(slab));
    }
    return nlohmann::json{
        {"name", allocator.name},
        {"type", allocator.type},
        {"encoding", allocator.encoding},
        {"item_size_bytes", allocator.item_size_bytes},
        {"metadata_file", allocator.metadata_file},
        {"slabs", slabs},
    };
}

bool allocatorFromJson(const nlohmann::json& json, SessionImageAllocatorManifest& allocator) {
    if (!json.is_object() || !json.contains("name") || !json.contains("type") ||
        !json.contains("encoding") || !json.contains("item_size_bytes") ||
        !json.contains("metadata_file") || !json.contains("slabs") ||
        !json["name"].is_string() || !json["type"].is_string() ||
        !json["encoding"].is_string() || !json["item_size_bytes"].is_number_unsigned() ||
        !json["metadata_file"].is_string() || !json["slabs"].is_array()) {
        return false;
    }

    allocator = {};
    allocator.name = json["name"].get<std::string>();
    allocator.type = json["type"].get<std::string>();
    allocator.encoding = json["encoding"].get<std::string>();
    allocator.item_size_bytes = json["item_size_bytes"].get<size_t>();
    allocator.metadata_file = json["metadata_file"].get<std::string>();
    if (allocator.name.empty() || allocator.type.empty() || allocator.encoding.empty() ||
        allocator.metadata_file.empty()) {
        return false;
    }

    std::vector<uint16_t> seen_indexes;
    for (const auto& slab_json : json["slabs"]) {
        SessionImageSlabFile slab;
        if (!slabFileFromJson(slab_json, slab)) {
            return false;
        }
        if (std::find(seen_indexes.begin(), seen_indexes.end(), slab.index) != seen_indexes.end()) {
            return false;
        }
        seen_indexes.push_back(slab.index);
        allocator.slabs.push_back(std::move(slab));
    }

    return true;
}

nlohmann::json manifestToJson(const SessionImageManifest& manifest) {
    nlohmann::json allocators = nlohmann::json::array();
    for (const auto& allocator : manifest.allocators) {
        allocators.push_back(allocatorToJson(allocator));
    }
    return nlohmann::json{
        {"version", manifest.version},
        {"session", manifest.session},
        {"roots_file", manifest.roots_file},
        {"allocators", allocators},
    };
}

bool manifestFromJson(const nlohmann::json& json, SessionImageManifest& manifest) {
    if (!json.is_object() || !json.contains("version") || !json.contains("session") ||
        !json.contains("roots_file") || !json.contains("allocators") ||
        !json["version"].is_number_unsigned() || !json["session"].is_string() ||
        !json["roots_file"].is_string() || !json["allocators"].is_array()) {
        return false;
    }

    manifest = {};
    manifest.version = json["version"].get<uint32_t>();
    manifest.session = json["session"].get<std::string>();
    manifest.roots_file = json["roots_file"].get<std::string>();
    if (manifest.version != 1 || manifest.session.empty() || manifest.roots_file.empty()) {
        return false;
    }

    std::vector<std::string> allocator_names;
    for (const auto& allocator_json : json["allocators"]) {
        SessionImageAllocatorManifest allocator;
        if (!allocatorFromJson(allocator_json, allocator)) {
            return false;
        }
        if (std::find(allocator_names.begin(), allocator_names.end(), allocator.name) != allocator_names.end()) {
            return false;
        }
        allocator_names.push_back(allocator.name);
        manifest.allocators.push_back(std::move(allocator));
    }

    return true;
}

} // namespace

SessionImageStore::SessionImageStore(std::string root_path, std::string session_name)
    : root_path_(std::move(root_path)),
      session_name_(sanitize_session_name(std::move(session_name))) {}

bool SessionImageStore::exists() const {
    return std::filesystem::exists(manifestPath());
}

void SessionImageStore::clear() const {
    std::error_code ec;
    std::filesystem::remove_all(sessionPath(), ec);
}

std::string SessionImageStore::sessionName() const {
    return session_name_;
}

std::string SessionImageStore::sessionPath() const {
    return (std::filesystem::path(root_path_) / session_name_).string();
}

std::string SessionImageStore::manifestPath() const {
    return (std::filesystem::path(sessionPath()) / "manifest.json").string();
}

bool SessionImageStore::saveManifest(const SessionImageManifest& manifest, std::string* error) const {
    SessionImageManifest normalized = manifest;
    normalized.version = 1;
    normalized.session = session_name_;
    return writeBinaryFile("manifest.json", manifestToJson(normalized).dump(2), error);
}

bool SessionImageStore::loadManifest(SessionImageManifest& manifest, std::string* error) const {
    std::string text;
    if (!readBinaryFile("manifest.json", text, error)) {
        return false;
    }

    try {
        auto json = nlohmann::json::parse(text);
        if (!manifestFromJson(json, manifest)) {
            if (error) *error = "invalid session manifest";
            return false;
        }
        if (manifest.session != session_name_) {
            if (error) *error = "session manifest does not match requested session";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("failed to parse session manifest: ") + e.what();
        return false;
    }
}

bool SessionImageStore::saveAllocatorMetadata(const SessionImageAllocatorManifest& allocator,
                                              const ArenaCheckpointMetadata& metadata,
                                              std::string* error) const {
    return writeBinaryFile(allocator.metadata_file,
                           serializeArenaCheckpointMetadata(metadata),
                           error);
}

bool SessionImageStore::loadAllocatorMetadata(const SessionImageAllocatorManifest& allocator,
                                              ArenaCheckpointMetadata& metadata,
                                              std::string* error) const {
    std::string text;
    if (!readBinaryFile(allocator.metadata_file, text, error)) {
        return false;
    }
    if (!deserializeArenaCheckpointMetadata(text, metadata)) {
        if (error) *error = "failed to decode allocator metadata";
        return false;
    }
    return true;
}

bool SessionImageStore::saveAllocatorSlabs(SessionImageAllocatorManifest& allocator,
                                           const std::vector<ArenaSlabImage>& slabs,
                                           std::string* error) const {
    if (allocator.slabs.size() != slabs.size()) {
        if (error) *error = "allocator manifest slab count does not match image count";
        return false;
    }

    for (size_t i = 0; i < slabs.size(); ++i) {
        if (allocator.slabs[i].index != slabs[i].slabIndex) {
            if (error) *error = "allocator manifest slab index does not match image";
            return false;
        }
        SessionImageSlabFile& slab_file = allocator.slabs[i];
        const std::string encoded = slabs[i].encoding == ArenaSlabEncoding::RawBytes
            ? encodeMmapAttachableRawSlabFile(slabs[i], slab_file)
            : encodeMmapFriendlySlabFile(slabs[i], slab_file);
        if (!writeBinaryFile(slab_file.file, encoded, error)) {
            return false;
        }
    }
    return true;
}

bool SessionImageStore::saveAllocatorAttachableStructuredSlabs(SessionImageAllocatorManifest& allocator,
                                                               const std::vector<ArenaSlabImage>& slabs,
                                                               std::string* error) const {
    if (allocator.slabs.size() != slabs.size()) {
        if (error) *error = "allocator manifest slab count does not match image count";
        return false;
    }

    for (size_t i = 0; i < slabs.size(); ++i) {
        if (allocator.slabs[i].index != slabs[i].slabIndex) {
            if (error) *error = "allocator manifest slab index does not match image";
            return false;
        }
        if (slabs[i].encoding != ArenaSlabEncoding::Structured) {
            if (error) *error = "attachable structured slab save requires structured images";
            return false;
        }
        SessionImageSlabFile& slab_file = allocator.slabs[i];
        if (!writeBinaryFile(slab_file.file,
                             encodeMmapAttachableStructuredSlabFile(slabs[i], allocator.item_size_bytes, slab_file),
                             error)) {
            return false;
        }
    }
    return true;
}

bool SessionImageStore::loadAllocatorSlabs(const SessionImageAllocatorManifest& allocator,
                                           std::vector<ArenaSlabImage>& slabs,
                                           std::string* error) const {
    slabs.clear();
    slabs.reserve(allocator.slabs.size());
    for (const auto& slab_file : allocator.slabs) {
        ArenaSlabImage slab;
        if (slab_file.format == kSessionImageMmapSlabFormat ||
            slab_file.format == kSessionImageMmapRawSlabFormat ||
            slab_file.format == kSessionImageMmapStructuredAttachSlabFormat) {
            const auto path = resolveRelativePath(slab_file.file);
            const int fd = ::open(path.c_str(), O_RDONLY);
            if (fd < 0) {
                if (error) *error = "failed to open mmap slab file for read";
                return false;
            }

            const auto file_size = std::filesystem::file_size(path);
            void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
            ::close(fd);
            if (mapped == MAP_FAILED) {
                if (error) *error = "failed to mmap slab file";
                return false;
            }

            const bool ok = slab_file.format == kSessionImageMmapRawSlabFormat
                ? decodeMmapAttachableRawSlab(mapped, static_cast<size_t>(file_size), slab_file, slab, error)
                : (slab_file.format == kSessionImageMmapStructuredAttachSlabFormat
                    ? decodeMmapAttachableStructuredSlab(mapped, static_cast<size_t>(file_size), slab_file, slab, error)
                    : decodeMmapFriendlySlabPayload(mapped, static_cast<size_t>(file_size), slab_file, slab, error));
            ::munmap(mapped, file_size);
            if (!ok) {
                return false;
            }
        } else {
            std::string text;
            if (!readBinaryFile(slab_file.file, text, error)) {
                return false;
            }
            if (!deserializeArenaSlabImage(text, slab)) {
                if (slab_file.format.empty()) {
                    const auto path = resolveRelativePath(slab_file.file);
                    const int fd = ::open(path.c_str(), O_RDONLY);
                    if (fd < 0) {
                        if (error) *error = "failed to open slab file for compatibility mmap read";
                        return false;
                    }
                    const auto file_size = std::filesystem::file_size(path);
                    void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
                    ::close(fd);
                    if (mapped == MAP_FAILED) {
                        if (error) *error = "failed to mmap slab file";
                        return false;
                    }
                    const bool ok = decodeMmapFriendlySlabPayload(mapped, static_cast<size_t>(file_size), slab_file, slab, error);
                    ::munmap(mapped, file_size);
                    if (!ok) {
                        if (error) *error = "failed to decode allocator slab image";
                        return false;
                    }
                } else {
                    if (error) *error = "failed to decode allocator slab image";
                    return false;
                }
            }
        }
        if (slab.slabIndex != slab_file.index) {
            if (error) *error = "allocator slab file index does not match decoded image";
            return false;
        }
        slabs.push_back(std::move(slab));
    }
    return true;
}

bool SessionImageStore::loadAllocatorMappedRawSlabs(const SessionImageAllocatorManifest& allocator,
                                                    std::vector<ArenaMappedRawSlabAttachment>& slabs,
                                                    std::string* error) const {
    slabs.clear();
    slabs.reserve(allocator.slabs.size());
    for (const auto& slab_file : allocator.slabs) {
        if (slab_file.format != kSessionImageMmapRawSlabFormat) {
            if (error) *error = "allocator slab is not in raw mmap-attachable format";
            return false;
        }

        const auto path = resolveRelativePath(slab_file.file);
        const int fd = ::open(path.c_str(), O_RDWR);
        if (fd < 0) {
            if (error) *error = "failed to open raw mmap slab file for attach";
            return false;
        }

        const auto file_size = std::filesystem::file_size(path);
        void* mapped = ::mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (mapped == MAP_FAILED) {
            if (error) *error = "failed to mmap raw slab file for attach";
            return false;
        }

        std::shared_ptr<void> mapped_region(mapped, [file_size](void* ptr) {
            if (ptr && ptr != MAP_FAILED) {
                ::munmap(ptr, file_size);
            }
        });

        ArenaMappedRawSlabAttachment slab;
        if (!mapMmapAttachableRawSlab(mapped_region.get(),
                                      static_cast<size_t>(file_size),
                                      slab_file,
                                      allocator.item_size_bytes,
                                      mapped_region,
                                      slab,
                                      error)) {
            return false;
        }
        slabs.push_back(std::move(slab));
    }
    return true;
}

bool SessionImageStore::loadAllocatorMappedStructuredSlabs(const SessionImageAllocatorManifest& allocator,
                                                           std::vector<ArenaMappedStructuredSlabAttachment>& slabs,
                                                           std::string* error) const {
    slabs.clear();
    slabs.reserve(allocator.slabs.size());
    for (const auto& slab_file : allocator.slabs) {
        if (slab_file.format != kSessionImageMmapStructuredAttachSlabFormat) {
            if (error) *error = "allocator slab is not in structured mmap-attachable format";
            return false;
        }

        const auto path = resolveRelativePath(slab_file.file);
        const int fd = ::open(path.c_str(), O_RDWR);
        if (fd < 0) {
            if (error) *error = "failed to open structured mmap slab file for attach";
            return false;
        }

        const auto file_size = std::filesystem::file_size(path);
        void* mapped = ::mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (mapped == MAP_FAILED) {
            if (error) *error = "failed to mmap structured slab file for attach";
            return false;
        }

        std::shared_ptr<void> mapped_region(mapped, [file_size](void* ptr) {
            if (ptr && ptr != MAP_FAILED) {
                ::munmap(ptr, file_size);
            }
        });

        ArenaMappedStructuredSlabAttachment slab;
        if (!mapMmapAttachableStructuredSlab(mapped_region.get(),
                                             static_cast<size_t>(file_size),
                                             slab_file,
                                             allocator.item_size_bytes,
                                             mapped_region,
                                             slab,
                                             error)) {
            return false;
        }
        slabs.push_back(std::move(slab));
    }
    return true;
}

bool SessionImageStore::saveRootState(const SessionImageManifest& manifest,
                                      const ArenaRootState& root_state,
                                      std::string* error) const {
    return writeBinaryFile(manifest.roots_file,
                           serializeArenaRootState(root_state),
                           error);
}

bool SessionImageStore::loadRootState(const SessionImageManifest& manifest,
                                      ArenaRootState& root_state,
                                      std::string* error) const {
    std::string text;
    if (!readBinaryFile(manifest.roots_file, text, error)) {
        return false;
    }
    if (!deserializeArenaRootState(text, root_state)) {
        if (error) *error = "failed to decode root state";
        return false;
    }
    return true;
}

std::string SessionImageStore::resolveRelativePath(const std::string& relative_path) const {
    return (std::filesystem::path(sessionPath()) / relative_path).string();
}

bool SessionImageStore::ensureParentDirectory(const std::string& relative_path, std::string* error) const {
    std::error_code ec;
    const auto parent = std::filesystem::path(resolveRelativePath(relative_path)).parent_path();
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        if (error) *error = std::string("failed to create session-image parent directory: ") + ec.message();
        return false;
    }
    return true;
}

bool SessionImageStore::writeBinaryFile(const std::string& relative_path,
                                        const std::string& contents,
                                        std::string* error) const {
    if (!ensureParentDirectory(relative_path, error)) {
        return false;
    }

    std::ofstream out(resolveRelativePath(relative_path), std::ios::binary | std::ios::trunc);
    if (!out.good()) {
        if (error) *error = "failed to open session-image file for write";
        return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!out.good()) {
        if (error) *error = "failed to write session-image file";
        return false;
    }
    return true;
}

bool SessionImageStore::readBinaryFile(const std::string& relative_path,
                                       std::string& contents,
                                       std::string* error) const {
    std::ifstream in(resolveRelativePath(relative_path), std::ios::binary);
    if (!in.good()) {
        if (error) *error = "failed to open session-image file for read";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        if (error) *error = "failed to read session-image file";
        return false;
    }
    contents = buffer.str();
    return true;
}

} // namespace agentc::runtime
