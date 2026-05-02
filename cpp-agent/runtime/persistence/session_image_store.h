#pragma once

#include "core/alloc.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace agentc::runtime {

struct SessionImageSlabFile {
    uint16_t index = 0;
    std::string file;
    std::string format;
    uint64_t payload_offset_bytes = 0;
    uint64_t payload_size_bytes = 0;
};

struct SessionImageAllocatorManifest {
    std::string name;
    std::string type;
    std::string encoding;
    size_t item_size_bytes = 0;
    std::string metadata_file;
    std::vector<SessionImageSlabFile> slabs;
};

struct SessionImageManifest {
    uint32_t version = 1;
    std::string session = "default";
    std::string roots_file = "roots.bin";
    std::vector<SessionImageAllocatorManifest> allocators;
};

class SessionImageStore {
public:
    explicit SessionImageStore(std::string root_path,
                               std::string session_name = "default");

    bool exists() const;
    void clear() const;

    std::string sessionName() const;
    std::string sessionPath() const;
    std::string manifestPath() const;

    bool saveManifest(const SessionImageManifest& manifest, std::string* error = nullptr) const;
    bool loadManifest(SessionImageManifest& manifest, std::string* error = nullptr) const;

    bool saveAllocatorMetadata(const SessionImageAllocatorManifest& allocator,
                               const ArenaCheckpointMetadata& metadata,
                               std::string* error = nullptr) const;
    bool loadAllocatorMetadata(const SessionImageAllocatorManifest& allocator,
                               ArenaCheckpointMetadata& metadata,
                               std::string* error = nullptr) const;

    bool saveAllocatorSlabs(SessionImageAllocatorManifest& allocator,
                            const std::vector<ArenaSlabImage>& slabs,
                            std::string* error = nullptr) const;
    bool saveAllocatorAttachableStructuredSlabs(SessionImageAllocatorManifest& allocator,
                                                const std::vector<ArenaSlabImage>& slabs,
                                                std::string* error = nullptr) const;
    bool loadAllocatorSlabs(const SessionImageAllocatorManifest& allocator,
                            std::vector<ArenaSlabImage>& slabs,
                            std::string* error = nullptr) const;
    bool loadAllocatorMappedRawSlabs(const SessionImageAllocatorManifest& allocator,
                                     std::vector<ArenaMappedRawSlabAttachment>& slabs,
                                     std::string* error = nullptr) const;
    bool loadAllocatorMappedStructuredSlabs(const SessionImageAllocatorManifest& allocator,
                                            std::vector<ArenaMappedStructuredSlabAttachment>& slabs,
                                            std::string* error = nullptr) const;

    bool saveRootState(const SessionImageManifest& manifest,
                       const ArenaRootState& root_state,
                       std::string* error = nullptr) const;
    bool loadRootState(const SessionImageManifest& manifest,
                       ArenaRootState& root_state,
                       std::string* error = nullptr) const;

private:
    std::string root_path_;
    std::string session_name_;

    std::string resolveRelativePath(const std::string& relative_path) const;
    bool ensureParentDirectory(const std::string& relative_path, std::string* error) const;
    bool writeBinaryFile(const std::string& relative_path,
                         const std::string& contents,
                         std::string* error) const;
    bool readBinaryFile(const std::string& relative_path,
                        std::string& contents,
                        std::string* error) const;
};

} // namespace agentc::runtime
