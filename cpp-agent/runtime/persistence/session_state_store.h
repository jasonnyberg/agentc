#pragma once

#include "core/alloc.h"
#include "listree/listree.h"
#include "session_image_store.h"

#include <nlohmann/json.hpp>
#include <string>

namespace agentc::runtime {

class SessionStateStore {
public:
    explicit SessionStateStore(std::string root_path,
                               std::string session_name = "default");

    // Configure all Listree/slab allocators with mmap-file backing pointed at
    // this session's directory.  Must be called before any allocations occur
    // (i.e. before constructing the EdictVM or creating any ListreeValues).
    // After this call, saveRoot becomes a simple flush+metadata write and
    // loadRoot reattaches existing slab files without deserialisation.
    void configureFileBackedAllocators();

    bool exists() const;
    bool loadRoot(CPtr<agentc::ListreeValue>& out,
                  std::vector<CPtr<agentc::ListreeValue>>* outStaticBases = nullptr,
                  agentc::ListreeStaticMountRegistry* registry = nullptr,
                  std::string* error = nullptr);
    bool saveRoot(CPtr<agentc::ListreeValue> root, std::string* error = nullptr) const;
    void clear() const;

    // Track static mounts associated with this session so they can be preserved on save
    std::vector<std::string> static_mounts;

private:
    std::string root_path_;
    std::string session_name_;
    bool file_backed_ = false;

    SessionImageStore sessionImageStore() const;

    // File-backed fast paths: flush + write metadata only (no copy/serialise).
    bool saveRootFileBacked(CPtr<agentc::ListreeValue> root, std::string* error) const;
    bool loadRootFileBacked(CPtr<agentc::ListreeValue>& out,
                            std::vector<CPtr<agentc::ListreeValue>>* outStaticBases,
                            agentc::ListreeStaticMountRegistry* registry,
                            std::string* error);
};

} // namespace agentc::runtime
