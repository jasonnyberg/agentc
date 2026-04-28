#pragma once
#include <string>
#include <utility>

// Google: read GEMINI_API_KEY from environment
std::string get_google_api_key();

// OpenAI: read OPENAI_API_KEY from environment
std::string get_openai_api_key();

// GitHub Copilot: read from ~/.pi/agent/auth.json, auto-refresh if expired
// Returns {access_token, base_url}
std::pair<std::string, std::string> get_copilot_credentials();
