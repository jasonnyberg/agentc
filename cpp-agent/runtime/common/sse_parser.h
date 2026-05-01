#pragma once

#include <functional>
#include <string>

namespace agentc::runtime {

struct SSEParser {
    std::function<void(const std::string&)> on_data;
    std::string buf;

    void feed(const char* data, size_t len);
    void finalize();
};

} // namespace agentc::runtime
