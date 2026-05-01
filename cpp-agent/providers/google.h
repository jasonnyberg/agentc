#pragma once

namespace agentc::runtime {
void register_google_provider();
}

inline void register_google_provider() {
    agentc::runtime::register_google_provider();
}
