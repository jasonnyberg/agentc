#include "provider_registry.h"

#include <stdexcept>
#include <unordered_map>

namespace agentc::runtime {
namespace {

std::unordered_map<std::string, StreamFn>& registry() {
    static std::unordered_map<std::string, StreamFn> instance;
    return instance;
}

} // namespace

void register_provider(const std::string& api, StreamFn fn) {
    registry()[api] = std::move(fn);
}

StreamFn get_provider(const std::string& api) {
    auto it = registry().find(api);
    if (it == registry().end()) {
        throw std::runtime_error("No provider registered for api: " + api);
    }
    return it->second;
}

} // namespace agentc::runtime
