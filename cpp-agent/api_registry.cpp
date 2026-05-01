#include "api_registry.h"

void register_provider(const std::string& api, StreamFn fn) {
    agentc::runtime::register_provider(api, std::move(fn));
}

StreamFn get_provider(const std::string& api) {
    return agentc::runtime::get_provider(api);
}
