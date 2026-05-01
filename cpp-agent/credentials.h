#pragma once

#include <string>
#include <utility>

namespace agentc::runtime {
std::string get_google_api_key();
std::string get_openai_api_key();
std::pair<std::string, std::string> get_copilot_credentials();
}

inline std::string get_google_api_key() {
    return agentc::runtime::get_google_api_key();
}

inline std::string get_openai_api_key() {
    return agentc::runtime::get_openai_api_key();
}

inline std::pair<std::string, std::string> get_copilot_credentials() {
    return agentc::runtime::get_copilot_credentials();
}
