#pragma once

#include <string>
#include <utility>

namespace agentc::runtime {

struct OpenAICodexCredentials {
    std::string access;
    std::string account_id;
};

std::string get_google_api_key();
std::string get_openai_api_key();
std::pair<std::string, std::string> get_copilot_credentials();
OpenAICodexCredentials get_openai_codex_credentials();

} // namespace agentc::runtime
