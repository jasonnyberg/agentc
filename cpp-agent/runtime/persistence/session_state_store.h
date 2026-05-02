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

    bool exists() const;
    bool loadRoot(CPtr<agentc::ListreeValue>& out, std::string* error = nullptr) const;
    bool saveRoot(CPtr<agentc::ListreeValue> root, std::string* error = nullptr) const;
    bool load(nlohmann::json& out, std::string* error = nullptr) const;
    bool save(const nlohmann::json& state, std::string* error = nullptr) const;
    void clear() const;

private:
    std::string root_path_;
    std::string session_name_;

    SessionImageStore sessionImageStore() const;
};

} // namespace agentc::runtime
