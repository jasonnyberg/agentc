// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

// G117 brokerage boundary — provider-owned auth/API request construction.
//
// This layer intentionally does not execute live HTTP or persist credentials.
// It produces native request specifications that can be executed by provider
// adapters while keeping secrets, tokens, sockets, and transport handles out of
// Edict/Listree/worker state.

#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace agentc::markethub {

struct BrokerageConfig {
    std::string clientId;
    std::string clientSecret;
    std::string redirectUri;
    std::string defaultSymbol = "AMD";
    int defaultStrikeCount = 20;
};

struct BrokerageToken {
    std::string accessToken;
    std::string refreshToken;
    long long expiresAtUnix = 0;
    long long refreshExpiresAtUnix = 0;
};

struct TokenStatus {
    bool hasAccessToken = false;
    bool hasRefreshToken = false;
    bool authenticated = false;
    bool accessTokenExpiresSoon = false;
    bool refreshTokenExpired = false;
    bool canRefresh = false;
};

TokenStatus brokerageTokenStatus(const BrokerageToken& token,
                                 long long nowUnix,
                                 long long accessBufferSeconds = 300,
                                 long long refreshBufferSeconds = 60);

struct HttpRequestSpec {
    std::string method = "GET";
    std::string url;
    // Headers are always safe previews. Authorization material is represented
    // by authScheme and redacted previews such as "Authorization: Bearer ***";
    // live credentials/tokens must stay in the native executor that consumes
    // the spec, never in Edict/Listree/worker-visible snapshots.
    std::vector<std::string> headers;
    // "" | "basic_client_credentials" | "bearer_token".
    std::string authScheme;
    std::map<std::string, std::string> formFields;
    // Form-field names whose values were intentionally redacted in formFields
    // and body (for example OAuth authorization codes and refresh tokens).
    std::vector<std::string> sensitiveFormFieldKeys;
    std::string body;
    bool redactAuthorization = true;
    bool supported = true;
    std::string unsupportedReason;
};

class BrokerageProvider {
public:
    virtual ~BrokerageProvider() = default;

    virtual std::string name() const = 0;
    virtual std::string backendName() const = 0;
    virtual bool isPreferred() const { return false; }

    virtual bool configure(const BrokerageConfig& cfg);
    virtual bool isConfigured() const;
    const BrokerageConfig& config() const { return cfg_; }

    virtual std::string authorizationUrl(const std::string& state) const = 0;
    virtual HttpRequestSpec tokenExchangeRequest(const std::string& code) const = 0;
    virtual HttpRequestSpec tokenRefreshRequest(const std::string& refreshToken) const = 0;

    // Provider-specific API request surfaces. Unsupported requests return a
    // supported=false spec rather than fabricating a route.
    virtual HttpRequestSpec optionChainRequest(const std::string& symbol,
                                               int strikeCount,
                                               const std::string& accessToken) const;
    virtual HttpRequestSpec optionSearchRequest(const std::string& symbol,
                                                int strikeCount,
                                                const std::string& accessToken) const;
    virtual HttpRequestSpec underlyingQuoteRequest(const std::string& symbol,
                                                   const std::string& accessToken) const;
    virtual HttpRequestSpec quoteBatchRequest(const std::vector<std::string>& symbols,
                                              const std::string& accessToken) const;
    virtual std::size_t maxQuoteBatchSize() const { return 0; }

protected:
    BrokerageConfig cfg_;
};

std::unique_ptr<BrokerageProvider> createBrokerageProvider(const std::string& name);

} // namespace agentc::markethub
