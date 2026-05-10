#include "credentials.h"

#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace agentc::runtime {
namespace {

using json = nlohmann::json;

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string auth_json_path() {
    const char* home = getenv("HOME");
    if (!home) throw std::runtime_error("HOME env var not set");
    return std::string(home) + "/.pi/agent/auth.json";
}

json read_auth_json() {
    std::ifstream f(auth_json_path());
    if (!f.good()) return json::object();
    try { return json::parse(f); }
    catch (...) { return json::object(); }
}

void write_auth_json(const json& data) {
    std::ofstream f(auth_json_path());
    f << data.dump(2);
}

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string http_get(const std::string& url, const std::vector<std::string>& headers) {
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

std::string copilot_base_url_from_token(const std::string& access_token) {
    std::regex re("proxy-ep=([^;]+)");
    std::smatch m;
    if (std::regex_search(access_token, m, re)) {
        std::string proxy_ep = m[1].str();
        auto pos = proxy_ep.find("proxy.");
        if (pos != std::string::npos)
            proxy_ep.replace(pos, 6, "api.");
        return "https://" + proxy_ep;
    }
    return "https://api.individual.githubcopilot.com";
}

const std::vector<std::string> COPILOT_STATIC_HEADERS = {
    "User-Agent: GitHubCopilotChat/0.35.0",
    "Editor-Version: vscode/1.107.0",
    "Editor-Plugin-Version: copilot-chat/0.35.0",
    "Copilot-Integration-Id: vscode-chat",
    "Accept: application/json",
};

std::pair<std::string, int64_t> refresh_copilot_token(const std::string& refresh_token) {
    std::vector<std::string> headers = COPILOT_STATIC_HEADERS;
    headers.push_back("Authorization: Bearer " + refresh_token);

    std::string body = http_get("https://api.github.com/copilot_internal/v2/token", headers);

    json resp = json::parse(body);
    std::string new_token = resp.at("token").get<std::string>();
    int64_t expires_at = resp.at("expires_at").get<int64_t>();
    int64_t expires_ms = expires_at * 1000LL - 300000LL;
    return {new_token, expires_ms};
}

std::string http_post_form(const std::string& url, const std::string& form_body) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");

    std::string body;
    curl_slist* hlist = nullptr;
    hlist = curl_slist_append(hlist, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hlist);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form_body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_slist_free_all(hlist);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error(std::string("curl POST failed: ") + curl_easy_strerror(res));
    }
    if (response_code >= 400) {
        throw std::runtime_error("HTTP POST failed with status " + std::to_string(response_code) + ": " + body);
    }
    return body;
}

std::string form_escape(const std::string& value) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("curl_easy_init failed");
    char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    if (!escaped) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("curl_easy_escape failed");
    }
    std::string out(escaped);
    curl_free(escaped);
    curl_easy_cleanup(curl);
    return out;
}

std::string base64url_decode(const std::string& input) {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64 = input;
    for (char& c : b64) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (b64.size() % 4 != 0) b64.push_back('=');

    std::vector<int> table(256, -1);
    for (int i = 0; i < static_cast<int>(alphabet.size()); ++i) {
        table[static_cast<unsigned char>(alphabet[i])] = i;
    }

    std::string out;
    int val = 0;
    int valb = -8;
    for (unsigned char c : b64) {
        if (c == '=') break;
        if (table[c] == -1) continue;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

json jwt_payload(const std::string& jwt) {
    const auto first = jwt.find('.');
    if (first == std::string::npos) throw std::runtime_error("Invalid JWT");
    const auto second = jwt.find('.', first + 1);
    if (second == std::string::npos) throw std::runtime_error("Invalid JWT");
    const std::string payload = jwt.substr(first + 1, second - first - 1);
    return json::parse(base64url_decode(payload));
}

std::string account_id_from_openai_codex_token(const std::string& access) {
    json payload = jwt_payload(access);
    const std::string claim = "https://api.openai.com/auth";
    if (!payload.contains(claim) || !payload[claim].is_object()) {
        throw std::runtime_error("OpenAI Codex JWT is missing auth claim");
    }
    const auto& auth = payload[claim];
    if (!auth.contains("chatgpt_account_id") || !auth["chatgpt_account_id"].is_string()) {
        throw std::runtime_error("OpenAI Codex JWT is missing chatgpt_account_id");
    }
    return auth["chatgpt_account_id"].get<std::string>();
}

json refresh_openai_codex_token(const std::string& refresh_token) {
    const std::string client_id = "app_EMoamEEZ73f0CkXaXp7hrann";
    std::string form = "grant_type=refresh_token";
    form += "&refresh_token=" + form_escape(refresh_token);
    form += "&client_id=" + form_escape(client_id);

    const std::string body = http_post_form("https://auth.openai.com/oauth/token", form);
    json resp = json::parse(body);
    if (!resp.contains("access_token") || !resp.contains("refresh_token") || !resp.contains("expires_in")) {
        throw std::runtime_error("OpenAI Codex refresh response missing required fields");
    }
    return resp;
}

} // namespace

std::string get_google_api_key() {
    if (const char* k = getenv("GEMINI_API_KEY")) return k;
    if (const char* k = getenv("GOOGLE_API_KEY")) return k;
    throw std::runtime_error("Google API key not found. Set GEMINI_API_KEY.");
}

std::string get_openai_api_key() {
    if (const char* k = getenv("OPENAI_API_KEY")) return k;
    throw std::runtime_error("OpenAI API key not found. Set OPENAI_API_KEY.");
}

std::pair<std::string, std::string> get_copilot_credentials() {
    for (const char* var : {"COPILOT_GITHUB_TOKEN", "GH_TOKEN", "GITHUB_TOKEN"}) {
        if (const char* k = getenv(var)) {
            try {
                auto [access, expires_ms] = refresh_copilot_token(k);
                (void)expires_ms;
                std::string base_url = copilot_base_url_from_token(access);
                return {access, base_url};
            } catch (...) {}
        }
    }

    json auth = read_auth_json();
    if (!auth.contains("github-copilot"))
        throw std::runtime_error("GitHub Copilot credentials not found in auth.json or env vars.");

    auto& cred = auth["github-copilot"];
    std::string access  = cred.value("access",  "");
    std::string refresh = cred.value("refresh", "");
    int64_t expires = cred.value("expires", static_cast<int64_t>(0));

    if (now_ms() >= expires && !refresh.empty()) {
        auto [new_access, new_expires] = refresh_copilot_token(refresh);
        access = new_access;
        expires = new_expires;
        auth["github-copilot"]["access"] = access;
        auth["github-copilot"]["expires"] = expires;
        write_auth_json(auth);
    }

    if (access.empty())
        throw std::runtime_error("GitHub Copilot access token is empty after refresh attempt.");

    std::string base_url = copilot_base_url_from_token(access);
    return {access, base_url};
}

OpenAICodexCredentials get_openai_codex_credentials() {
    json auth = read_auth_json();
    if (!auth.contains("openai-codex")) {
        throw std::runtime_error("OpenAI Codex credentials not found in ~/.pi/agent/auth.json. Run pi /login and select ChatGPT Plus/Pro (Codex).");
    }

    auto& cred = auth["openai-codex"];
    std::string access = cred.value("access", "");
    std::string refresh = cred.value("refresh", "");
    std::string account_id = cred.value("accountId", "");
    int64_t expires = cred.value("expires", static_cast<int64_t>(0));

    if (now_ms() >= expires - 300000LL && !refresh.empty()) {
        json refreshed = refresh_openai_codex_token(refresh);
        access = refreshed.at("access_token").get<std::string>();
        refresh = refreshed.at("refresh_token").get<std::string>();
        expires = now_ms() + refreshed.at("expires_in").get<int64_t>() * 1000LL;
        account_id = account_id_from_openai_codex_token(access);

        auth["openai-codex"]["type"] = "oauth";
        auth["openai-codex"]["access"] = access;
        auth["openai-codex"]["refresh"] = refresh;
        auth["openai-codex"]["expires"] = expires;
        auth["openai-codex"]["accountId"] = account_id;
        write_auth_json(auth);
    }

    if (access.empty()) {
        throw std::runtime_error("OpenAI Codex access token is empty.");
    }
    if (account_id.empty()) {
        account_id = account_id_from_openai_codex_token(access);
    }
    return OpenAICodexCredentials{access, account_id};
}

} // namespace agentc::runtime
