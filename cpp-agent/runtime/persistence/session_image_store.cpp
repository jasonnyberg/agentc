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
constexpr size_t kAllocatorIdentityBytes = 64;

struct SessionImageMmapSlabHeader {
    char magic[8];
    uint32_t version = kSessionImageMmapSlabVersion;
    uint32_t payload_offset_bytes = 0;
    uint32_t payload_size_bytes = 0;
    uint16_t slab_index = 0;
    uint8_t encoding = 0;
    uint8_t reserved = 0;
    char allocator_name[kAllocatorIdentityBytes] = {};
    char allocator_type[kAllocatorIdentityBytes] = {};
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

bool hasExpectedSlabMagic(const SessionImageMmapSlabHeader& header) {
    const auto magic = slabMagicBytes();
    return std::memcmp(header.magic, magic.data(), magic.size()) == 0;
}

void setAllocatorIdentity(char (&target)[kAllocatorIdentityBytes], const std::string& value) {
    std::memset(target, 0, sizeof(target));
    if (!value.empty()) {
        std::strncpy(target, value.c_str(), sizeof(target) - 1);
    }
}

std::string readAllocatorIdentity(const char (&source)[kAllocatorIdentityBytes]) {
    return std::string(source, strnlen(source, sizeof(source)));
}

template <typename Header>
bool validateAllocatorIdentity(const Header& header,
                               const SessionImageAllocatorManifest& allocator,
                               std::string* error) {
    const std::string header_name = readAllocatorIdentity(header.allocator_name);
    const std::string header_type = readAllocatorIdentity(header.allocator_type);
    if (!allocator.name.empty() && !header_name.empty() && header_name != allocator.name) {
        if (error) *error = "slab header allocator name does not match manifest";
        return false;
    }
    if (!allocator.type.empty() && !header_type.empty() && header_type != allocator.type) {
        if (error) *error = "slab header allocator type does not match manifest";
        return false;
    }
    return true;
}

std::string encodeMmapFriendlySlabFile(const SessionImageAllocatorManifest& allocator,
                                       const ArenaSlabImage& slab,
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
    setAllocatorIdentity(header.allocator_name, allocator.name);
    setAllocatorIdentity(header.allocator_type, allocator.type);

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

bool decodeMmapFriendlySlabPayload(const void* mapped_data,
                                   size_t mapped_size,
                                   const SessionImageAllocatorManifest& allocator,
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
    if (!validateAllocatorIdentity(header, allocator, error)) {
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

nlohmann::json bootstrapAllocatorToJson(const SessionImageBootstrapAllocator& allocator) {
    return nlohmann::json{
        {"name", allocator.name},
        {"type", allocator.type},
        {"encoding", allocator.encoding},
        {"item_size_bytes", allocator.item_size_bytes},
        {"metadata_file", allocator.metadata_file},
    };
}

bool bootstrapAllocatorFromJson(const nlohmann::json& json, SessionImageBootstrapAllocator& allocator) {
    if (!json.is_object() || !json.contains("name") || !json.contains("type") ||
        !json.contains("encoding") || !json.contains("item_size_bytes") ||
        !json.contains("metadata_file") ||
        !json["name"].is_string() || !json["type"].is_string() ||
        !json["encoding"].is_string() || !json["item_size_bytes"].is_number_unsigned() ||
        !json["metadata_file"].is_string()) {
        return false;
    }

    allocator = {};
    allocator.name = json["name"].get<std::string>();
    allocator.type = json["type"].get<std::string>();
    allocator.encoding = json["encoding"].get<std::string>();
    allocator.item_size_bytes = json["item_size_bytes"].get<size_t>();
    allocator.metadata_file = json["metadata_file"].get<std::string>();
    return !allocator.name.empty() && !allocator.type.empty() && !allocator.encoding.empty() &&
        !allocator.metadata_file.empty();
}

nlohmann::json bootstrapToJson(const SessionImageBootstrap& bootstrap) {
    nlohmann::json allocators = nlohmann::json::array();
    for (const auto& allocator : bootstrap.allocators) {
        allocators.push_back(bootstrapAllocatorToJson(allocator));
    }
    return nlohmann::json{
        {"version", bootstrap.version},
        {"session", bootstrap.session},
        {"roots_file", bootstrap.roots_file},
        {"allocators", allocators},
    };
}

bool bootstrapFromJson(const nlohmann::json& json, SessionImageBootstrap& bootstrap) {
    if (!json.is_object() || !json.contains("version") || !json.contains("session") ||
        !json.contains("roots_file") || !json.contains("allocators") ||
        !json["version"].is_number_unsigned() || !json["session"].is_string() ||
        !json["roots_file"].is_string() || !json["allocators"].is_array()) {
        return false;
    }

    bootstrap = {};
    bootstrap.version = json["version"].get<uint32_t>();
    bootstrap.session = json["session"].get<std::string>();
    bootstrap.roots_file = json["roots_file"].get<std::string>();
    if (bootstrap.version != 1 || bootstrap.session.empty() || bootstrap.roots_file.empty()) {
        return false;
    }

    std::vector<std::string> allocator_names;
    for (const auto& allocator_json : json["allocators"]) {
        SessionImageBootstrapAllocator allocator;
        if (!bootstrapAllocatorFromJson(allocator_json, allocator)) {
            return false;
        }
        if (std::find(allocator_names.begin(), allocator_names.end(), allocator.name) != allocator_names.end()) {
            return false;
        }
        allocator_names.push_back(allocator.name);
        bootstrap.allocators.push_back(std::move(allocator));
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

std::string allocatorDirectoryRelativePath(const SessionImageAllocatorManifest& allocator) {
    const auto metadata_parent = std::filesystem::path(allocator.metadata_file).parent_path();
    if (!metadata_parent.empty()) {
        return metadata_parent.generic_string();
    }
    if (!allocator.name.empty()) {
        return (std::filesystem::path("allocators") / allocator.name).generic_string();
    }
    return "allocators";
}

bool isAllocatorSlabCandidate(const std::filesystem::directory_entry& entry,
                              const SessionImageAllocatorManifest& allocator) {
    if (!entry.is_regular_file()) {
        return false;
    }
    const auto filename = entry.path().filename().string();
    const auto metadata_name = std::filesystem::path(allocator.metadata_file).filename().string();
    if (!metadata_name.empty() && filename == metadata_name) {
        return false;
    }
    return filename.rfind("slab.", 0) == 0 && entry.path().extension() == ".bin";
}

const SessionImageSlabFile* findAllocatorSlabByIndex(const std::vector<SessionImageSlabFile>& slabs,
                                                     uint16_t index) {
    for (const auto& slab : slabs) {
        if (slab.index == index) {
            return &slab;
        }
    }
    return nullptr;
}

bool resolveAllocatorSlabsForLoad(const SessionImageStore& image_store,
                                  const SessionImageAllocatorManifest& allocator,
                                  std::vector<SessionImageSlabFile>& resolved,
                                  std::string* error) {
    std::vector<SessionImageSlabFile> discovered;
    if (!image_store.discoverAllocatorSlabs(allocator, discovered, error)) {
        return false;
    }

    if (allocator.slabs.empty()) {
        resolved = std::move(discovered);
        return true;
    }

    resolved = allocator.slabs;
    for (auto& slab : resolved) {
        const auto* discovered_slab = findAllocatorSlabByIndex(discovered, slab.index);
        if (!discovered_slab) {
            continue;
        }
        if (slab.file != discovered_slab->file) {
            if (error) *error = "manifest slab file does not match discovered slab file";
            return false;
        }
        if (slab.format.empty()) {
            slab.format = discovered_slab->format;
        } else if (slab.format != discovered_slab->format) {
            if (error) *error = "manifest slab format does not match discovered slab header";
            return false;
        }
        if (slab.payload_offset_bytes == 0) {
            slab.payload_offset_bytes = discovered_slab->payload_offset_bytes;
        } else if (slab.payload_offset_bytes != discovered_slab->payload_offset_bytes) {
            if (error) *error = "manifest slab payload offset does not match discovered slab header";
            return false;
        }
        if (slab.payload_size_bytes == 0) {
            slab.payload_size_bytes = discovered_slab->payload_size_bytes;
        } else if (slab.payload_size_bytes != discovered_slab->payload_size_bytes) {
            if (error) *error = "manifest slab payload size does not match discovered slab header";
            return false;
        }
    }
    return true;
}

} // namespace

SessionImageStore::SessionImageStore(std::string root_path, std::string session_name)
    : root_path_(std::move(root_path)),
      session_name_(sanitize_session_name(std::move(session_name))) {}

bool SessionImageStore::exists() const {
    return std::filesystem::exists(bootstrapPath()) || std::filesystem::exists(manifestPath());
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

std::string SessionImageStore::bootstrapPath() const {
    return (std::filesystem::path(sessionPath()) / "bootstrap.json").string();
}

std::string SessionImageStore::manifestPath() const {
    return (std::filesystem::path(sessionPath()) / "manifest.json").string();
}

bool SessionImageStore::saveBootstrap(const SessionImageBootstrap& bootstrap, std::string* error) const {
    SessionImageBootstrap normalized = bootstrap;
    normalized.version = 1;
    normalized.session = session_name_;
    return writeBinaryFile("bootstrap.json", bootstrapToJson(normalized).dump(2), error);
}

bool SessionImageStore::loadBootstrap(SessionImageBootstrap& bootstrap, std::string* error) const {
    std::string text;
    if (!readBinaryFile("bootstrap.json", text, error)) {
        return false;
    }

    try {
        auto json = nlohmann::json::parse(text);
        if (!bootstrapFromJson(json, bootstrap)) {
            if (error) *error = "invalid session bootstrap";
            return false;
        }
        if (bootstrap.session != session_name_) {
            if (error) *error = "session bootstrap does not match requested session";
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        if (error) *error = std::string("failed to parse session bootstrap: ") + e.what();
        return false;
    }
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

bool SessionImageStore::discoverAllocatorSlabs(const SessionImageAllocatorManifest& allocator,
                                              std::vector<SessionImageSlabFile>& slabs,
                                              std::string* error) const {
    slabs.clear();

    const auto relative_dir = allocatorDirectoryRelativePath(allocator);
    const auto session_root = std::filesystem::path(sessionPath());
    const auto absolute_dir = session_root / relative_dir;
    std::error_code ec;
    if (!std::filesystem::exists(absolute_dir, ec) || ec) {
        return !ec;
    }
    if (!std::filesystem::is_directory(absolute_dir, ec) || ec) {
        if (error) *error = "allocator slab path is not a directory";
        return false;
    }

    for (const auto& entry : std::filesystem::directory_iterator(absolute_dir)) {
        if (!isAllocatorSlabCandidate(entry, allocator)) {
            continue;
        }

        const auto relative_path = entry.path().lexically_relative(session_root).generic_string();
        SessionImageSlabHeaderInfo info;
        if (!inspectSlabFile(relative_path, info, error)) {
            return false;
        }
        if (!allocator.name.empty() && !info.allocator_name.empty() && info.allocator_name != allocator.name) {
            if (error) *error = "discovered slab header allocator name does not match requested allocator";
            return false;
        }
        if (!allocator.type.empty() && !info.allocator_type.empty() && info.allocator_type != allocator.type) {
            if (error) *error = "discovered slab header allocator type does not match requested allocator";
            return false;
        }
        if (findAllocatorSlabByIndex(slabs, info.index)) {
            if (error) *error = "allocator slab discovery found duplicate slab index";
            return false;
        }

        SessionImageSlabFile slab;
        slab.index = info.index;
        slab.file = relative_path;
        slab.format = info.format;
        slab.payload_offset_bytes = info.payload_offset_bytes;
        slab.payload_size_bytes = info.payload_size_bytes;
        slabs.push_back(std::move(slab));
    }

    std::sort(slabs.begin(), slabs.end(), [](const SessionImageSlabFile& lhs, const SessionImageSlabFile& rhs) {
        return lhs.index < rhs.index;
    });
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
        const std::string encoded = encodeMmapFriendlySlabFile(allocator, slabs[i], slab_file);
        if (!writeBinaryFile(slab_file.file, encoded, error)) {
            return false;
        }
    }
    return true;
}

bool SessionImageStore::loadAllocatorSlabs(const SessionImageAllocatorManifest& allocator,
                                           std::vector<ArenaSlabImage>& slabs,
                                           std::string* error) const {
    std::vector<SessionImageSlabFile> slab_files;
    if (!resolveAllocatorSlabsForLoad(*this, allocator, slab_files, error)) {
        return false;
    }

    slabs.clear();
    slabs.reserve(slab_files.size());
    for (const auto& slab_file : slab_files) {
        ArenaSlabImage slab;
        if (slab_file.format == kSessionImageMmapSlabFormat) {
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
            const bool ok = decodeMmapFriendlySlabPayload(
                mapped, static_cast<size_t>(file_size), allocator, slab_file, slab, error);
            ::munmap(mapped, file_size);
            if (!ok) return false;
        } else {
            std::string text;
            if (!readBinaryFile(slab_file.file, text, error)) return false;
            if (!deserializeArenaSlabImage(text, slab)) {
                if (error) *error = "failed to decode allocator slab image";
                return false;
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

bool SessionImageStore::inspectSlabFile(const std::string& relative_path,
                                        SessionImageSlabHeaderInfo& info,
                                        std::string* error) const {
    const auto path = resolveRelativePath(relative_path);
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        if (error) *error = "failed to open slab file for inspection";
        return false;
    }

    const auto file_size = std::filesystem::file_size(path);
    void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (mapped == MAP_FAILED) {
        if (error) *error = "failed to mmap slab file for inspection";
        return false;
    }

    info = {};
    const auto finalize = [&](bool ok) {
        ::munmap(mapped, file_size);
        return ok;
    };

    if (static_cast<size_t>(file_size) < sizeof(SessionImageMmapSlabHeader)) {
        if (error) *error = "slab file is smaller than the minimum header";
        return finalize(false);
    }

    SessionImageMmapSlabHeader common_header;
    std::memcpy(&common_header, mapped, sizeof(common_header));
    if (hasExpectedSlabMagic(common_header) && common_header.version == kSessionImageMmapSlabVersion) {
        info.index = common_header.slab_index;
        info.format = kSessionImageMmapSlabFormat;
        info.allocator_name = readAllocatorIdentity(common_header.allocator_name);
        info.allocator_type = readAllocatorIdentity(common_header.allocator_type);
        info.encoding = static_cast<ArenaSlabEncoding>(common_header.encoding);
        info.payload_offset_bytes = common_header.payload_offset_bytes;
        info.payload_size_bytes = common_header.payload_size_bytes;
        return finalize(true);
    }

    if (error) *error = "unrecognized slab header format";
    return finalize(false);
}

bool SessionImageStore::saveRootState(const std::string& roots_file,
                                      const ArenaRootState& root_state,
                                      std::string* error) const {
    return writeBinaryFile(roots_file,
                           serializeArenaRootState(root_state),
                           error);
}

bool SessionImageStore::loadRootState(const std::string& roots_file,
                                      ArenaRootState& root_state,
                                      std::string* error) const {
    std::string text;
    if (!readBinaryFile(roots_file, text, error)) {
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
