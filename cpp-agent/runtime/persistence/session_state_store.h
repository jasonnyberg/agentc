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
    bool loadRoot(CPtr<agentc::ListreeValue>& out, std::string* error = nullptr) const;
    bool saveRoot(CPtr<agentc::ListreeValue> root, std::string* error = nullptr) const;
    void clear() const;

private:
    std::string root_path_;
    std::string session_name_;
    bool file_backed_ = false;

    SessionImageStore sessionImageStore() const;

    // File-backed fast paths: flush + write metadata only (no copy/serialise).
    bool saveRootFileBacked(CPtr<agentc::ListreeValue> root, std::string* error) const;
    bool loadRootFileBacked(CPtr<agentc::ListreeValue>& out, std::string* error) const;
};

} // namespace agentc::runtime
