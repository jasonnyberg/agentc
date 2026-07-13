// This file is part of AgentC.
//
// G117 Phase 4 live-provider smoke harness. This executable is intentionally
// outside the std-only markethub library: it is a native/operator tool that
// reads local credential/token files, injects bearer auth inside the process,
// executes provider request specs with libcurl, and publishes a minimal live
// underlying quote as a canonical MarketHub event. It never prints tokens or
// client secrets.

#include "markethub/brokerage_provider.h"
#include "markethub/market_hub.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mh = agentc::markethub;

namespace {

struct Args {
    std::string provider = "tradestation";
    std::string symbol;
    std::string configDir;
    std::string mode = "underlying"; // underlying | option-search | option-chain | auth-url | token-status
    int strikeCount = 12;
    bool raw = false;
};

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string error;
};

std::string envOr(const char* name, const std::string& fallback = "") {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

std::string defaultConfigDir() {
    std::string configured = envOr("AGENTC_MARKETHUB_CONFIG_DIR");
    if (!configured.empty()) return configured;
    configured = envOr("GREEKSCOPE_CONFIG_DIR");
    if (!configured.empty()) return configured;
    const std::string home = envOr("HOME", ".");
    return home + "/GreekScope/config";
}

std::string joinPath(const std::string& dir, const std::string& leaf) {
    if (dir.empty()) return leaf;
    if (dir.back() == '/') return dir + leaf;
    return dir + "/" + leaf;
}

std::string readText(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

std::string extractJsonString(const std::string& text,
                              const std::string& key,
                              const std::string& fallback = "") {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return fallback;
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) return fallback;
    const std::size_t start = pos + 1;
    std::string result;
    bool escaped = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        const char ch = text[i];
        if (escaped) {
            result.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') return result;
        result.push_back(ch);
    }
    return fallback;
}

long long extractJsonInt(const std::string& text,
                         const std::string& key,
                         long long fallback = 0) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) return fallback;
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return fallback;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str() + pos, &end, 10);
    return end && end != text.c_str() + pos ? value : fallback;
}

bool extractJsonNumber(const std::string& text, const std::string& key, double& out) {
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = text.find(needle);
    if (pos == std::string::npos) return false;
    pos = text.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    ++pos;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    if (pos < text.size() && text[pos] == '"') ++pos;
    char* end = nullptr;
    const double value = std::strtod(text.c_str() + pos, &end);
    if (!end || end == text.c_str() + pos) return false;
    out = value;
    return true;
}

bool extractFirstNumber(const std::string& text,
                        const std::vector<std::string>& keys,
                        double& out) {
    for (const auto& key : keys) {
        if (extractJsonNumber(text, key, out)) return true;
    }
    return false;
}

mh::BrokerageConfig loadConfig(const std::string& provider,
                               const std::string& configDir) {
    const std::string path = joinPath(configDir, provider + ".local.json");
    const std::string text = readText(path);
    mh::BrokerageConfig cfg;
    cfg.clientId = extractJsonString(text, "client_id");
    cfg.clientSecret = extractJsonString(text, "client_secret");
    cfg.redirectUri = extractJsonString(text, "redirect_uri");
    cfg.defaultSymbol = extractJsonString(text, "symbol", "AMD");
    cfg.defaultStrikeCount = static_cast<int>(extractJsonInt(text, "strike_count", 12));
    return cfg;
}

mh::BrokerageToken loadToken(const std::string& provider,
                             const std::string& configDir) {
    const std::string path = joinPath(configDir, provider + "_tokens.local.json");
    const std::string text = readText(path);
    mh::BrokerageToken token;
    token.accessToken = extractJsonString(text, "access_token");
    token.refreshToken = extractJsonString(text, "refresh_token");
    token.expiresAtUnix = extractJsonInt(text, "expires_at_unix", 0);
    token.refreshExpiresAtUnix = extractJsonInt(text, "refresh_expires_at_unix", 0);
    return token;
}

size_t curlWrite(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

HttpResponse executeGetWithBearer(const mh::HttpRequestSpec& spec,
                                  const std::string& accessToken) {
    HttpResponse response;
    if (spec.method != "GET") {
        response.error = "live smoke only executes GET specs";
        return response;
    }
    if (spec.authScheme != "bearer_token") {
        response.error = "GET spec is not bearer-token authenticated";
        return response;
    }
    if (accessToken.empty()) {
        response.error = "missing access token";
        return response;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl_easy_init failed";
        return response;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    const std::string auth = "Authorization: Bearer " + accessToken;
    headers = curl_slist_append(headers, auth.c_str());

    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, spec.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    if (code != CURLE_OK) {
        response.error = errbuf[0] ? errbuf : curl_easy_strerror(code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

mh::MarketEvent normalizeUnderlyingQuote(const std::string& provider,
                                         const std::string& symbol,
                                         const std::string& body) {
    mh::MarketEvent event;
    event.type = mh::EventType::UnderlyingQuote;
    event.symbol = upper(symbol);
    event.source = provider + "_rest_live";

    double value = 0.0;
    if (extractFirstNumber(body, {"Bid", "bidPrice", "bid", "bid_price"}, value)) {
        event.num["bid"] = value;
    }
    if (extractFirstNumber(body, {"Ask", "askPrice", "ask", "ask_price"}, value)) {
        event.num["ask"] = value;
    }
    if (extractFirstNumber(body, {"Last", "lastPrice", "last", "last_price"}, value)) {
        event.num["last"] = value;
        event.num["mark"] = value;
    }
    if (extractFirstNumber(body, {"Volume", "totalVolume", "volume"}, value)) {
        event.num["volume"] = value;
    }
    return event;
}

void usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0 << " [--provider tradestation|schwab] [--symbol AMD] "
        << "[--strike-count N] [--config-dir DIR] [--mode underlying|option-search|option-chain|auth-url|token-status] [--raw]\n\n"
        << "Credentials are read from DIR/<provider>.local.json and DIR/<provider>_tokens.local.json.\n"
        << "Default DIR: AGENTC_MARKETHUB_CONFIG_DIR, then GREEKSCOPE_CONFIG_DIR, then ~/GreekScope/config.\n"
        << "The harness injects bearer tokens only inside the native process and never prints them.\n";
}

bool parseArgs(int argc, char** argv, Args& args) {
    args.configDir = defaultConfigDir();
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto needValue = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << name << " requires a value\n";
                return "";
            }
            return argv[++i];
        };
        if (arg == "--provider") {
            args.provider = needValue("--provider");
        } else if (arg == "--symbol") {
            args.symbol = needValue("--symbol");
        } else if (arg == "--strike-count") {
            args.strikeCount = std::max(1, std::atoi(needValue("--strike-count").c_str()));
        } else if (arg == "--config-dir") {
            args.configDir = needValue("--config-dir");
        } else if (arg == "--mode") {
            args.mode = needValue("--mode");
        } else if (arg == "--raw") {
            args.raw = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            return false;
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            usage(argv[0]);
            return false;
        }
    }
    args.provider = upper(args.provider);
    std::transform(args.provider.begin(), args.provider.end(), args.provider.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) return 2;

    auto provider = mh::createBrokerageProvider(args.provider);
    if (!provider) {
        std::cerr << "unknown provider: " << args.provider << "\n";
        return 2;
    }

    mh::BrokerageConfig cfg = loadConfig(args.provider, args.configDir);
    provider->configure(cfg);
    if (args.symbol.empty()) args.symbol = cfg.defaultSymbol.empty() ? "AMD" : cfg.defaultSymbol;
    if (args.strikeCount <= 0) args.strikeCount = cfg.defaultStrikeCount;

    if (args.mode == "auth-url") {
        if (!provider->isConfigured()) {
            std::cerr << "provider config missing client_id/client_secret/redirect_uri in "
                      << args.configDir << "\n";
            return 2;
        }
        std::cout << provider->authorizationUrl("agentc-markethub-smoke") << "\n";
        return 0;
    }

    mh::BrokerageToken token = loadToken(args.provider, args.configDir);
    const long long now = static_cast<long long>(std::time(nullptr));
    const mh::TokenStatus status = mh::brokerageTokenStatus(token, now);
    std::cout << "provider=" << provider->name()
              << " preferred=" << (provider->isPreferred() ? "true" : "false")
              << " configured=" << (provider->isConfigured() ? "true" : "false")
              << " authenticated=" << (status.authenticated ? "true" : "false")
              << " can_refresh=" << (status.canRefresh ? "true" : "false")
              << " config_dir=" << args.configDir << "\n";

    if (args.mode == "token-status") return 0;

    if (!status.authenticated) {
        std::cerr << "access token is missing or expiring; run OAuth/refresh outside Edict, "
                  << "then rerun this harness. No secrets were printed.\n";
        return 3;
    }

    mh::HttpRequestSpec spec;
    if (args.mode == "underlying") {
        spec = provider->underlyingQuoteRequest(args.symbol, token.accessToken);
    } else if (args.mode == "option-search") {
        spec = provider->optionSearchRequest(args.symbol, args.strikeCount, token.accessToken);
    } else if (args.mode == "option-chain") {
        spec = provider->optionChainRequest(args.symbol, args.strikeCount, token.accessToken);
    } else {
        std::cerr << "unknown mode: " << args.mode << "\n";
        return 2;
    }

    std::cout << "request_method=" << spec.method << " request_url=" << spec.url << "\n";
    HttpResponse response = executeGetWithBearer(spec, token.accessToken);
    std::cout << "http_status=" << response.status
              << " body_bytes=" << response.body.size() << "\n";
    if (!response.error.empty()) {
        std::cerr << "request_error=" << response.error << "\n";
        return 4;
    }
    if (response.status < 200 || response.status >= 300) {
        if (args.raw && !response.body.empty()) std::cout << response.body << "\n";
        return 5;
    }

    if (args.raw && !response.body.empty()) {
        std::cout << response.body << "\n";
    }

    if (args.mode == "underlying") {
        mh::MarketEvent event = normalizeUnderlyingQuote(provider->name(), args.symbol, response.body);
        if (!event.num.empty()) {
            mh::MarketHub hub;
            mh::EnvelopeCollectorSubscriber collector;
            hub.subscribe(&collector, {{mh::EventType::UnderlyingQuote}, {upper(args.symbol)}});
            hub.publish(event);
            std::cout << "normalized_underlying_event=true\n";
            std::cout << collector.collectJson() << "\n";
        } else {
            std::cout << "normalized_underlying_event=false\n";
        }
    }

    return 0;
}
