#include "ai_types.h"
#include <functional>
#include <string>

// SSE parser: shared by all HTTP providers.
// Feeds raw bytes from libcurl into line-by-line SSE events.
// Calls on_data for each complete "data: {...}" payload.
// Phase 3 implementation.

struct SSEParser {
    std::function<void(const std::string&)> on_data;
    std::string buf;

    void feed(const char* data, size_t len) {
        buf.append(data, len);
        // Process complete lines separated by \n
        size_t pos = 0, end;
        while ((end = buf.find('\n', pos)) != std::string::npos) {
            std::string line = buf.substr(pos, end - pos);
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (line.rfind("data: ", 0) == 0) {
                std::string payload = line.substr(6);
                if (payload == "[DONE]") {
                    // Signal end of stream
                    if (on_data) on_data("[DONE]");
                } else if (!payload.empty() && on_data) {
                    on_data(payload);
                }
            }
            pos = end + 1;
        }
        buf = buf.substr(pos);
    }
};
