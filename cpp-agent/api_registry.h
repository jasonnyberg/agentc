#pragma once
#include "ai_types.h"
#include <functional>
#include <string>
#include <unordered_map>

void     register_provider(const std::string& api, StreamFn fn);
StreamFn get_provider(const std::string& api);
