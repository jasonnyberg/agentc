#include "sse_parser.h"

namespace agentc::runtime {

void SSEParser::feed(const char* data, size_t len) {
    buf.append(data, len);
    size_t pos = 0, end;
    while ((end = buf.find('\n', pos)) != std::string::npos) {
        std::string line = buf.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("data: ", 0) == 0) {
            std::string payload = line.substr(6);
            if (!payload.empty() && on_data) {
                on_data(payload);
            }
        }
        pos = end + 1;
    }
    buf = buf.substr(pos);
}

void SSEParser::finalize() {
    if (on_data) on_data("[DONE]");
}

} // namespace agentc::runtime
