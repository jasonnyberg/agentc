#pragma once

#include "core/alloc.h"
#include "listree/listree.h"

#include <nlohmann/json.hpp>
#include <string>

namespace agentc::runtime {

class SessionStateStore {
public:
    explicit SessionStateStore(std::string base_path);

    bool exists() const;
    bool loadRoot(CPtr<agentc::ListreeValue>& out, std::string* error = nullptr) const;
    bool saveRoot(CPtr<agentc::ListreeValue> root, std::string* error = nullptr) const;
    bool load(nlohmann::json& out, std::string* error = nullptr) const;
    bool save(const nlohmann::json& state, std::string* error = nullptr) const;
    void clear() const;

private:
    std::string base_path_;

    std::string valuePath() const;
    std::string refPath() const;
    std::string nodePath() const;
    std::string itemPath() const;
    std::string treePath() const;
    std::string statePath() const;

    void removeStaleFiles() const;
};

} // namespace agentc::runtime
