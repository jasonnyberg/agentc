#include "credentials.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

// ─── Helpers ─────────────────────────────────────────────────────────────────

static int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::string auth_json_path() {
    const char* home = getenv("HOME");
    if (!home) throw std::runtime_error("HOME env var not set");
    return std::string(home) + "/.pi/agent/auth.json";
}

static json read_auth_json() {
    std::ifstream f(auth_json_path());
    if (!f.good()) return json::object();
    try { return json::parse(f); }
    catch (...) { return json::object(); }
}

static void write_auth_json(const json& data) {
    std::ofstream f(auth_json_path());
    f << data.dump(2);
}

// ─── libcurl write callback ─────────────────────────────────────────────────

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

static std::string http_get(const std::string& url,
                             const std::vector<std::string>& headers) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string body;
    curl_slist* hlist = nullptr;
    for (auto& h : headers) hlist = curl_slist_append(hlist, h.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        throw std::runtime_error(std::string("curl GET failed: ") + curl_easy_strerror(res));
    return body;
}

// ─── Copilot base URL from access token ─────────────────────────────────────

static std::string copilot_base_url_from_token(const std::string& access_token) {
    std::regex re("proxy-ep=([^;]+)");
    std::smatch m;
    if (std::regex_search(access_token, m, re)) {
        std::string proxy_ep = m[1].str();
        // proxy.individual.githubcopilot.com → api.individual.githubcopilot.com
        auto pos = proxy_ep.find("proxy.");
        if (pos != std::string::npos)
            proxy_ep.replace(pos, 6, "api.");
        return "https://" + proxy_ep;
    }
    return "https://api.individual.githubcopilot.com";
}

// ─── Copilot token refresh ────────────────────────────────────────────────────
// GET https://api.github.com/copilot_internal/v2/token
// Authorization: Bearer {refresh_token}
// Response: { "token": "tid=...", "expires_at": <unix_seconds> }
// Write back: access = token, expires = expires_at * 1000 - 300000

static const std::vector<std::string> COPILOT_STATIC_HEADERS = {
    "User-Agent: GitHubCopilotChat/0.35.0",
    "Editor-Version: vscode/1.107.0",
    "Editor-Plugin-Version: copilot-chat/0.35.0",
    "Copilot-Integration-Id: vscode-chat",
    "Accept: application/json",
};

static std::pair<std::string, int64_t> refresh_copilot_token(const std::string& refresh_token) {
    std::vector<std::string> headers = COPILOT_STATIC_HEADERS;
    headers.push_back("Authorization: Bearer " + refresh_token);

    std::string body = http_get(
        "https://api.github.com/copilot_internal/v2/token",
        headers
    );

    json resp = json::parse(body);
    std::string new_token = resp.at("token").get<std::string>();
    int64_t expires_at    = resp.at("expires_at").get<int64_t>();
    int64_t expires_ms    = expires_at * 1000LL - 300000LL;  // 5 min buffer (Pi convention)
    return {new_token, expires_ms};
}

// ─── Public API ──────────────────────────────────────────────────────────────

std::string get_google_api_key() {
    if (const char* k = getenv("GEMINI_API_KEY")) return k;
    if (const char* k = getenv("GOOGLE_API_KEY"))  return k;
    throw std::runtime_error("Google API key not found. Set GEMINI_API_KEY.");
}

std::string get_openai_api_key() {
    if (const char* k = getenv("OPENAI_API_KEY")) return k;
    throw std::runtime_error("OpenAI API key not found. Set OPENAI_API_KEY.");
}

std::pair<std::string, std::string> get_copilot_credentials() {
    // 1. Env var fallback
    for (const char* var : {"COPILOT_GITHUB_TOKEN", "GH_TOKEN", "GITHUB_TOKEN"}) {
        if (const char* k = getenv(var)) {
            // Env var is a GH token — treat as refresh token and get a Copilot token
            try {
                auto [access, expires_ms] = refresh_copilot_token(k);
                std::string base_url = copilot_base_url_from_token(access);
                return {access, base_url};
            } catch (...) {}
        }
    }

    // 2. auth.json
    json auth = read_auth_json();
    if (!auth.contains("github-copilot"))
        throw std::runtime_error("GitHub Copilot credentials not found in auth.json or env vars.");

    auto& cred = auth["github-copilot"];
    std::string access  = cred.value("access",  "");
    std::string refresh = cred.value("refresh", "");
    int64_t     expires = cred.value("expires", (int64_t)0);

    // 3. Refresh if expired
    if (now_ms() >= expires && !refresh.empty()) {
        auto [new_access, new_expires] = refresh_copilot_token(refresh);
        access  = new_access;
        expires = new_expires;
        // Write back to auth.json
        auth["github-copilot"]["access"]  = access;
        auth["github-copilot"]["expires"] = expires;
        write_auth_json(auth);
    }

    if (access.empty())
        throw std::runtime_error("GitHub Copilot access token is empty after refresh attempt.");

    std::string base_url = copilot_base_url_from_token(access);
    return {access, base_url};
}
