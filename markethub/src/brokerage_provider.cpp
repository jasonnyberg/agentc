// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include "markethub/brokerage_provider.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace agentc::markethub {

namespace {

constexpr const char* kTradeStationAuthUrl =
    "https://signin.tradestation.com/authorize";
constexpr const char* kTradeStationTokenUrl =
    "https://signin.tradestation.com/oauth/token";
constexpr const char* kTradeStationApiHost = "https://api.tradestation.com";
constexpr const char* kTradeStationBackendName =
    "greekscope_backend_tradestation_rest";
constexpr int kTradeStationOptionSearchExpiryDays = 36;
constexpr std::size_t kTradeStationBatchQuoteLimit = 100;

constexpr const char* kSchwabAuthUrl =
    "https://api.schwabapi.com/v1/oauth/authorize";
constexpr const char* kSchwabTokenUrl =
    "https://api.schwabapi.com/v1/oauth/token";
constexpr const char* kSchwabApiHost = "https://api.schwabapi.com";
constexpr const char* kSchwabBackendName = "greekscope_backend_schwab_rest";

std::string uppercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

std::string urlEncode(const std::string& text) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (unsigned char ch : text) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
            ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else {
            out << '%' << std::setw(2) << std::setfill('0')
                << static_cast<int>(ch);
        }
    }
    return out.str();
}

std::string formBody(const std::map<std::string, std::string>& fields) {
    std::ostringstream out;
    bool first = true;
    for (const auto& [key, value] : fields) {
        if (!first) {
            out << '&';
        }
        first = false;
        out << urlEncode(key) << '=' << urlEncode(value);
    }
    return out.str();
}

HttpRequestSpec unsupported(const std::string& reason) {
    HttpRequestSpec spec;
    spec.supported = false;
    spec.unsupportedReason = reason;
    return spec;
}

void addBearer(HttpRequestSpec& spec) {
    spec.headers.push_back("Accept: application/json");
    spec.headers.push_back("Authorization: Bearer ***");
    spec.authScheme = "bearer_token";
    spec.redactAuthorization = true;
}

HttpRequestSpec postFormRequest(const std::string& url,
                                const std::map<std::string, std::string>& fields,
                                const BrokerageConfig& cfg,
                                const std::vector<std::string>& sensitiveKeys) {
    (void)cfg;
    HttpRequestSpec spec;
    spec.method = "POST";
    spec.url = url;
    spec.authScheme = "basic_client_credentials";
    spec.sensitiveFormFieldKeys = sensitiveKeys;
    spec.formFields = fields;
    for (const auto& key : sensitiveKeys) {
        auto it = spec.formFields.find(key);
        if (it != spec.formFields.end()) {
            it->second = "***";
        }
    }
    spec.body = formBody(spec.formFields);
    spec.headers.push_back("Content-Type: application/x-www-form-urlencoded");
    spec.headers.push_back("Authorization: Basic ***");
    spec.redactAuthorization = true;
    return spec;
}

std::string joinEncodedSymbols(const std::vector<std::string>& symbols) {
    std::ostringstream out;
    for (std::size_t i = 0; i < symbols.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << urlEncode(symbols[i]);
    }
    return out.str();
}

class TradeStationBrokerageProvider final : public BrokerageProvider {
public:
    std::string name() const override { return "tradestation"; }
    std::string backendName() const override { return kTradeStationBackendName; }
    bool isPreferred() const override { return true; }

    std::string authorizationUrl(const std::string& state) const override {
        return std::string(kTradeStationAuthUrl) +
               "?response_type=code&client_id=" + urlEncode(cfg_.clientId) +
               "&redirect_uri=" + urlEncode(cfg_.redirectUri) +
               "&state=" + urlEncode(state) +
               "&audience=" + urlEncode("https://api.tradestation.com") +
               "&scope=" + urlEncode("openid offline_access MarketData");
    }

    HttpRequestSpec tokenExchangeRequest(const std::string& code) const override {
        return postFormRequest(
            kTradeStationTokenUrl,
            {{"grant_type", "authorization_code"},
             {"code", code},
             {"redirect_uri", cfg_.redirectUri}},
            cfg_, {"code"});
    }

    HttpRequestSpec tokenRefreshRequest(const std::string& refreshToken) const override {
        return postFormRequest(kTradeStationTokenUrl,
                               {{"grant_type", "refresh_token"},
                                {"refresh_token", refreshToken}},
                               cfg_, {"refresh_token"});
    }

    HttpRequestSpec optionChainRequest(const std::string& symbol,
                                       int strikeCount,
                                       const std::string& accessToken) const override {
        return optionSearchRequest(symbol, strikeCount, accessToken);
    }

    HttpRequestSpec optionSearchRequest(const std::string& symbol,
                                        int strikeCount,
                                        const std::string& accessToken) const override {
        (void)accessToken;
        const int strikes = std::max(1, strikeCount);
        const std::string criteria = "C=SO&R=" + urlEncode(uppercase(symbol)) +
                                     "&Stk=" + std::to_string(strikes) +
                                     "&Exd=" + std::to_string(kTradeStationOptionSearchExpiryDays) +
                                     "&OT=Both";
        HttpRequestSpec spec;
        spec.method = "GET";
        spec.url = std::string(kTradeStationApiHost) +
                   "/v2/data/symbols/search/" + criteria;
        addBearer(spec);
        return spec;
    }

    HttpRequestSpec underlyingQuoteRequest(const std::string& symbol,
                                           const std::string& accessToken) const override {
        (void)accessToken;
        HttpRequestSpec spec;
        spec.method = "GET";
        spec.url = std::string(kTradeStationApiHost) + "/v3/marketdata/quotes/" +
                   urlEncode(uppercase(symbol));
        addBearer(spec);
        return spec;
    }

    HttpRequestSpec quoteBatchRequest(const std::vector<std::string>& symbols,
                                      const std::string& accessToken) const override {
        (void)accessToken;
        HttpRequestSpec spec;
        spec.method = "GET";
        spec.url = std::string(kTradeStationApiHost) + "/v3/marketdata/quotes/" +
                   joinEncodedSymbols(symbols);
        addBearer(spec);
        return spec;
    }

    std::size_t maxQuoteBatchSize() const override {
        return kTradeStationBatchQuoteLimit;
    }
};

class SchwabBrokerageProvider final : public BrokerageProvider {
public:
    std::string name() const override { return "schwab"; }
    std::string backendName() const override { return kSchwabBackendName; }

    std::string authorizationUrl(const std::string& state) const override {
        return std::string(kSchwabAuthUrl) +
               "?response_type=code&client_id=" + urlEncode(cfg_.clientId) +
               "&redirect_uri=" + urlEncode(cfg_.redirectUri) +
               "&state=" + urlEncode(state);
    }

    HttpRequestSpec tokenExchangeRequest(const std::string& code) const override {
        return postFormRequest(kSchwabTokenUrl,
                               {{"grant_type", "authorization_code"},
                                {"code", code},
                                {"redirect_uri", cfg_.redirectUri}},
                               cfg_, {"code"});
    }

    HttpRequestSpec tokenRefreshRequest(const std::string& refreshToken) const override {
        return postFormRequest(kSchwabTokenUrl,
                               {{"grant_type", "refresh_token"},
                                {"refresh_token", refreshToken}},
                               cfg_, {"refresh_token"});
    }

    HttpRequestSpec optionChainRequest(const std::string& symbol,
                                       int strikeCount,
                                       const std::string& accessToken) const override {
        (void)accessToken;
        HttpRequestSpec spec;
        spec.method = "GET";
        spec.url = std::string(kSchwabApiHost) +
                   "/marketdata/v1/chains?symbol=" + urlEncode(uppercase(symbol)) +
                   "&contractType=ALL&strategy=SINGLE&includeQuotes=TRUE"
                   "&toDate=2028-12-31";
        if (strikeCount > 0) {
            spec.url += "&strikeCount=" + std::to_string(std::max(1, strikeCount));
        }
        addBearer(spec);
        return spec;
    }

    HttpRequestSpec optionSearchRequest(const std::string& symbol,
                                        int strikeCount,
                                        const std::string& accessToken) const override {
        return optionChainRequest(symbol, strikeCount, accessToken);
    }

    HttpRequestSpec underlyingQuoteRequest(const std::string& symbol,
                                           const std::string& accessToken) const override {
        (void)accessToken;
        HttpRequestSpec spec;
        spec.method = "GET";
        spec.url = std::string(kSchwabApiHost) + "/marketdata/v1/quotes?symbols=" +
                   urlEncode(uppercase(symbol));
        addBearer(spec);
        return spec;
    }

    HttpRequestSpec quoteBatchRequest(const std::vector<std::string>& symbols,
                                      const std::string& accessToken) const override {
        (void)accessToken;
        HttpRequestSpec spec;
        spec.method = "GET";
        spec.url = std::string(kSchwabApiHost) + "/marketdata/v1/quotes?symbols=" +
                   joinEncodedSymbols(symbols);
        addBearer(spec);
        return spec;
    }

    std::size_t maxQuoteBatchSize() const override { return 100; }
};

} // namespace

TokenStatus brokerageTokenStatus(const BrokerageToken& token,
                                 long long nowUnix,
                                 long long accessBufferSeconds,
                                 long long refreshBufferSeconds) {
    TokenStatus status;
    status.hasAccessToken = !token.accessToken.empty();
    status.hasRefreshToken = !token.refreshToken.empty();
    status.accessTokenExpiresSoon =
        token.expiresAtUnix > 0 && nowUnix + accessBufferSeconds >= token.expiresAtUnix;
    status.refreshTokenExpired =
        !status.hasRefreshToken ||
        (token.refreshExpiresAtUnix > 0 &&
         nowUnix + refreshBufferSeconds >= token.refreshExpiresAtUnix);
    status.authenticated = status.hasAccessToken && !status.accessTokenExpiresSoon;
    status.canRefresh = status.hasRefreshToken && !status.refreshTokenExpired;
    return status;
}

bool BrokerageProvider::configure(const BrokerageConfig& cfg) {
    cfg_ = cfg;
    return isConfigured();
}

bool BrokerageProvider::isConfigured() const {
    return !cfg_.clientId.empty() && !cfg_.clientSecret.empty() &&
           !cfg_.redirectUri.empty();
}

HttpRequestSpec BrokerageProvider::optionChainRequest(const std::string&,
                                                      int,
                                                      const std::string&) const {
    return unsupported("option chain request is unsupported by provider");
}

HttpRequestSpec BrokerageProvider::optionSearchRequest(const std::string&,
                                                       int,
                                                       const std::string&) const {
    return unsupported("option search request is unsupported by provider");
}

HttpRequestSpec BrokerageProvider::underlyingQuoteRequest(const std::string&,
                                                          const std::string&) const {
    return unsupported("underlying quote request is unsupported by provider");
}

HttpRequestSpec BrokerageProvider::quoteBatchRequest(const std::vector<std::string>&,
                                                     const std::string&) const {
    return unsupported("quote batch request is unsupported by provider");
}

std::unique_ptr<BrokerageProvider> createBrokerageProvider(const std::string& name) {
    const std::string normalized = uppercase(name);
    if (normalized == "TRADESTATION" || normalized == "TS") {
        return std::make_unique<TradeStationBrokerageProvider>();
    }
    if (normalized == "SCHWAB") {
        return std::make_unique<SchwabBrokerageProvider>();
    }
    return nullptr;
}

} // namespace agentc::markethub
