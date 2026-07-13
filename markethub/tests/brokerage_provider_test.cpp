// This file is part of AgentC.
//
// G117 brokerage boundary coverage: provider-owned auth/API request builders
// mirror the working GreekScope/DeltaGUI C++ backend providers without moving
// credentials, sockets, or live HTTP execution into Edict/hub state.

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "markethub/brokerage_provider.h"

using namespace agentc::markethub;

namespace {

BrokerageConfig sampleConfig() {
    BrokerageConfig cfg;
    cfg.clientId = "client-123";
    cfg.clientSecret = "secret-456";
    cfg.redirectUri = "http://localhost:8080/callback";
    cfg.defaultSymbol = "AMD";
    cfg.defaultStrikeCount = 12;
    return cfg;
}

bool hasHeader(const HttpRequestSpec& request, const std::string& header) {
    for (const auto& candidate : request.headers) {
        if (candidate == header) {
            return true;
        }
    }
    return false;
}

bool hasSensitiveField(const HttpRequestSpec& request, const std::string& key) {
    return std::find(request.sensitiveFormFieldKeys.begin(),
                     request.sensitiveFormFieldKeys.end(), key) !=
           request.sensitiveFormFieldKeys.end();
}

bool specPublicFieldsContain(const HttpRequestSpec& request,
                             const std::string& needle) {
    if (request.url.find(needle) != std::string::npos ||
        request.body.find(needle) != std::string::npos) {
        return true;
    }
    for (const auto& header : request.headers) {
        if (header.find(needle) != std::string::npos) return true;
    }
    for (const auto& [key, value] : request.formFields) {
        if (key.find(needle) != std::string::npos ||
            value.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(BrokerageProviderTest, TradeStationAuthRequestsMatchDeltaGuiProvider) {
    auto provider = createBrokerageProvider("tradestation");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name(), "tradestation");
    EXPECT_EQ(provider->backendName(), "greekscope_backend_tradestation_rest");
    EXPECT_TRUE(provider->isPreferred());

    provider->configure(sampleConfig());
    ASSERT_TRUE(provider->isConfigured());

    const std::string url = provider->authorizationUrl("agentc-state");
    EXPECT_NE(url.find("https://signin.tradestation.com/authorize?"), std::string::npos);
    EXPECT_NE(url.find("response_type=code"), std::string::npos);
    EXPECT_NE(url.find("client_id=client-123"), std::string::npos);
    EXPECT_NE(url.find("redirect_uri=http%3A%2F%2Flocalhost%3A8080%2Fcallback"), std::string::npos);
    EXPECT_NE(url.find("state=agentc-state"), std::string::npos);
    EXPECT_NE(url.find("audience=https%3A%2F%2Fapi.tradestation.com"), std::string::npos);
    EXPECT_NE(url.find("scope=openid%20offline_access%20MarketData"), std::string::npos);

    const HttpRequestSpec exchange = provider->tokenExchangeRequest("code-abc");
    EXPECT_EQ(exchange.method, "POST");
    EXPECT_EQ(exchange.url, "https://signin.tradestation.com/oauth/token");
    EXPECT_EQ(exchange.formFields.at("grant_type"), "authorization_code");
    EXPECT_EQ(exchange.formFields.at("code"), "***");
    EXPECT_EQ(exchange.formFields.at("redirect_uri"), sampleConfig().redirectUri);
    EXPECT_EQ(exchange.authScheme, "basic_client_credentials");
    EXPECT_TRUE(hasSensitiveField(exchange, "code"));
    EXPECT_TRUE(hasHeader(exchange, "Content-Type: application/x-www-form-urlencoded"));
    EXPECT_TRUE(hasHeader(exchange, "Authorization: Basic ***"));
    EXPECT_TRUE(exchange.redactAuthorization);
    EXPECT_FALSE(specPublicFieldsContain(exchange, "secret-456"));
    EXPECT_FALSE(specPublicFieldsContain(exchange, "code-abc"));

    const HttpRequestSpec refresh = provider->tokenRefreshRequest("refresh-xyz");
    EXPECT_EQ(refresh.url, "https://signin.tradestation.com/oauth/token");
    EXPECT_EQ(refresh.formFields.at("grant_type"), "refresh_token");
    EXPECT_EQ(refresh.formFields.at("refresh_token"), "***");
    EXPECT_TRUE(hasSensitiveField(refresh, "refresh_token"));
    EXPECT_TRUE(hasHeader(refresh, "Authorization: Basic ***"));
    EXPECT_FALSE(specPublicFieldsContain(refresh, "secret-456"));
    EXPECT_FALSE(specPublicFieldsContain(refresh, "refresh-xyz"));
}

TEST(BrokerageProviderTest, TradeStationMarketApiRequestsUseSearchAndBatchQuotes) {
    auto provider = createBrokerageProvider("tradestation");
    ASSERT_NE(provider, nullptr);
    provider->configure(sampleConfig());

    const HttpRequestSpec search = provider->optionSearchRequest("amd", 12, "access-token");
    EXPECT_EQ(search.method, "GET");
    EXPECT_EQ(search.url,
              "https://api.tradestation.com/v2/data/symbols/search/"
              "C=SO&R=AMD&Stk=12&Exd=36&OT=Both");
    EXPECT_EQ(search.authScheme, "bearer_token");
    EXPECT_TRUE(hasHeader(search, "Accept: application/json"));
    EXPECT_TRUE(hasHeader(search, "Authorization: Bearer ***"));
    EXPECT_FALSE(specPublicFieldsContain(search, "access-token"));

    const HttpRequestSpec underlying = provider->underlyingQuoteRequest("amd", "access-token");
    EXPECT_EQ(underlying.url, "https://api.tradestation.com/v3/marketdata/quotes/AMD");
    EXPECT_EQ(underlying.authScheme, "bearer_token");
    EXPECT_FALSE(specPublicFieldsContain(underlying, "access-token"));

    const HttpRequestSpec quotes = provider->quoteBatchRequest(
        {"AMD", "AMD 260116C120", "AMD 260116P115.5"}, "access-token");
    EXPECT_EQ(quotes.url,
              "https://api.tradestation.com/v3/marketdata/quotes/"
              "AMD,AMD%20260116C120,AMD%20260116P115.5");
    EXPECT_EQ(quotes.authScheme, "bearer_token");
    EXPECT_FALSE(specPublicFieldsContain(quotes, "access-token"));
    EXPECT_EQ(provider->maxQuoteBatchSize(), 100u);
}

TEST(BrokerageProviderTest, SchwabAuthAndChainRequestsMatchDeltaGuiProvider) {
    auto provider = createBrokerageProvider("schwab");
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name(), "schwab");
    EXPECT_EQ(provider->backendName(), "greekscope_backend_schwab_rest");
    EXPECT_FALSE(provider->isPreferred());

    provider->configure(sampleConfig());
    ASSERT_TRUE(provider->isConfigured());

    const std::string url = provider->authorizationUrl("agentc-state");
    EXPECT_NE(url.find("https://api.schwabapi.com/v1/oauth/authorize?"), std::string::npos);
    EXPECT_NE(url.find("response_type=code"), std::string::npos);
    EXPECT_NE(url.find("client_id=client-123"), std::string::npos);
    EXPECT_NE(url.find("redirect_uri=http%3A%2F%2Flocalhost%3A8080%2Fcallback"), std::string::npos);
    EXPECT_NE(url.find("state=agentc-state"), std::string::npos);
    EXPECT_EQ(url.find("audience="), std::string::npos);
    EXPECT_EQ(url.find("scope="), std::string::npos);

    const HttpRequestSpec exchange = provider->tokenExchangeRequest("code-abc");
    EXPECT_EQ(exchange.method, "POST");
    EXPECT_EQ(exchange.url, "https://api.schwabapi.com/v1/oauth/token");
    EXPECT_EQ(exchange.formFields.at("grant_type"), "authorization_code");
    EXPECT_EQ(exchange.formFields.at("code"), "***");
    EXPECT_EQ(exchange.authScheme, "basic_client_credentials");
    EXPECT_TRUE(hasSensitiveField(exchange, "code"));
    EXPECT_TRUE(hasHeader(exchange, "Authorization: Basic ***"));
    EXPECT_FALSE(specPublicFieldsContain(exchange, "secret-456"));
    EXPECT_FALSE(specPublicFieldsContain(exchange, "code-abc"));

    const HttpRequestSpec chain = provider->optionChainRequest("amd", 12, "access-token");
    EXPECT_EQ(chain.method, "GET");
    EXPECT_NE(chain.url.find("https://api.schwabapi.com/marketdata/v1/chains?symbol=AMD"), std::string::npos);
    EXPECT_NE(chain.url.find("contractType=ALL"), std::string::npos);
    EXPECT_NE(chain.url.find("strategy=SINGLE"), std::string::npos);
    EXPECT_NE(chain.url.find("includeQuotes=TRUE"), std::string::npos);
    EXPECT_NE(chain.url.find("toDate=2028-12-31"), std::string::npos);
    EXPECT_NE(chain.url.find("strikeCount=12"), std::string::npos);
    EXPECT_EQ(chain.authScheme, "bearer_token");
    EXPECT_TRUE(hasHeader(chain, "Authorization: Bearer ***"));
    EXPECT_FALSE(specPublicFieldsContain(chain, "access-token"));

    const HttpRequestSpec fullChain = provider->optionChainRequest("AMD", 0, "access-token");
    EXPECT_EQ(fullChain.url.find("strikeCount="), std::string::npos);
}

TEST(BrokerageProviderTest, TokenStatusCapturesAccessAndRefreshWindows) {
    BrokerageToken token;
    token.accessToken = "access";
    token.refreshToken = "refresh";
    token.expiresAtUnix = 1300;
    token.refreshExpiresAtUnix = 5000;

    TokenStatus fresh = brokerageTokenStatus(token, 900);
    EXPECT_TRUE(fresh.authenticated);
    EXPECT_FALSE(fresh.accessTokenExpiresSoon);
    EXPECT_TRUE(fresh.canRefresh);

    TokenStatus expiring = brokerageTokenStatus(token, 1100);
    EXPECT_FALSE(expiring.authenticated);
    EXPECT_TRUE(expiring.accessTokenExpiresSoon);
    EXPECT_TRUE(expiring.canRefresh);

    TokenStatus refreshExpired = brokerageTokenStatus(token, 5000);
    EXPECT_FALSE(refreshExpired.canRefresh);
    EXPECT_TRUE(refreshExpired.refreshTokenExpired);
}

TEST(BrokerageProviderTest, FactoryRejectsUnknownProvider) {
    EXPECT_EQ(createBrokerageProvider("unknown"), nullptr);
}
