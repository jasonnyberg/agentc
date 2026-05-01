#pragma once

namespace agentc::runtime {
void register_openai_provider();
}

inline void register_openai_provider() {
    agentc::runtime::register_openai_provider();
}
