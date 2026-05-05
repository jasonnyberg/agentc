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

struct SessionImageSlabHeaderInfo {
    uint16_t index = 0;
    std::string format;
    std::string allocator_name;
    std::string allocator_type;
    ArenaSlabEncoding encoding = ArenaSlabEncoding::RawBytes;
    uint64_t payload_offset_bytes = 0;
    uint64_t payload_size_bytes = 0;
};

struct SessionImageBootstrapAllocator {
    std::string name;
    std::string type;
    std::string encoding;
    size_t item_size_bytes = 0;
    std::string metadata_file;
};

struct SessionImageBootstrap {
    uint32_t version = 1;
    std::string session = "default";
    std::string roots_file = "roots.bin";
    std::vector<SessionImageBootstrapAllocator> allocators;
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
    std::string bootstrapPath() const;
    std::string manifestPath() const;

    bool saveBootstrap(const SessionImageBootstrap& bootstrap, std::string* error = nullptr) const;
    bool loadBootstrap(SessionImageBootstrap& bootstrap, std::string* error = nullptr) const;
    bool saveManifest(const SessionImageManifest& manifest, std::string* error = nullptr) const;
    bool loadManifest(SessionImageManifest& manifest, std::string* error = nullptr) const;

    bool saveAllocatorMetadata(const SessionImageAllocatorManifest& allocator,
                               const ArenaCheckpointMetadata& metadata,
                               std::string* error = nullptr) const;
    bool loadAllocatorMetadata(const SessionImageAllocatorManifest& allocator,
                               ArenaCheckpointMetadata& metadata,
                               std::string* error = nullptr) const;
    bool discoverAllocatorSlabs(const SessionImageAllocatorManifest& allocator,
                                std::vector<SessionImageSlabFile>& slabs,
                                std::string* error = nullptr) const;

    bool saveAllocatorSlabs(SessionImageAllocatorManifest& allocator,
                            const std::vector<ArenaSlabImage>& slabs,
                            std::string* error = nullptr) const;
    bool loadAllocatorSlabs(const SessionImageAllocatorManifest& allocator,
                            std::vector<ArenaSlabImage>& slabs,
                            std::string* error = nullptr) const;

    bool saveRootState(const std::string& roots_file,
                       const ArenaRootState& root_state,
                       std::string* error = nullptr) const;
    bool loadRootState(const std::string& roots_file,
                       ArenaRootState& root_state,
                       std::string* error = nullptr) const;
    bool inspectSlabFile(const std::string& relative_path,
                         SessionImageSlabHeaderInfo& info,
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
