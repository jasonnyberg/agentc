#pragma once

#include "ai_types.h"

#include <string>

namespace agentc::runtime {

void register_provider(const std::string& api, StreamFn fn);
StreamFn get_provider(const std::string& api);

} // namespace agentc::runtime
