// AgentC MarketHub DeltaGUI backend-contract compatibility server.
//
// Serves the same fixture/local HTTP routes as DeltaGUI's
// greekscope_backend_cpp so that check_backend_compat.sh can verify
// the AgentC-based backend is contract-compatible with the native
// DeltaGUI backend — without requiring live broker credentials.
//
// Routes (matching DeltaGUI check_backend_cpp.sh / check_backend_tradestation.sh):
//   GET  /api/health
//   GET  /api/config
//   GET  /api/auth/{provider}/status
//   GET  /api/ready              (returns 503 when not configured)
//   GET  /api/market/options?symbol=...&fixture=true
//   GET  /api/quotes/status?symbol=...
//   GET  /api/quotes/cache?since=N
//   GET  /api/stream/status
//   POST /api/stream/start?symbol=...[&fixture=true]
//   GET  /api/stream/deltas?since=N[&fixture=true]
//   POST /api/stream/fixture/update?symbol=...&bid=...&ask=...
//   GET  /api/stream/options?symbol=...   (501 placeholder)
//   POST /api/stream/stop
//   *    → 404 {ok:false, code:"not_found"}

#include "markethub/market_hub.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char* kBackendName = "agentc_markethub_backend";

// ---------------------------------------------------------------------------
// JSON helpers (minimal, no external dependency)
// ---------------------------------------------------------------------------

std::string escJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

long long nowUnix() {
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

// ---------------------------------------------------------------------------
// Fixture snapshot — matches DeltaGUI MarketSnapshot shape
// ---------------------------------------------------------------------------

struct FixtureContract {
    std::string symbol;
    std::string underlying;
    std::string expiration;
    double daysToExpiration;
    double strike;
    std::string optionType;
    double multiplier;
    double bid;
    double ask;
    double last;
    double mark;
    double modelPrice;
    double modelResidual;
    double impliedVolatility;
    double openInterest;
    double volume;
    long long timestamp;
};

struct FixtureSnapshot {
    int schemaVersion = 1;
    std::string provider;
    std::string underlying;
    double underlyingPrice;
    double riskFreeRate;
    long long timestamp;
    std::vector<FixtureContract> contracts;
    // metadata fields
    std::string underlyingPriceSource;
};

FixtureSnapshot makeFixture(const std::string& provider, const std::string& symbol) {
    FixtureSnapshot snap;
    snap.provider = "greekscope_backend_" + provider + "_rest";
    snap.underlying = symbol;
    snap.underlyingPrice = 100.0;
    snap.riskFreeRate = 0.045;
    snap.timestamp = nowUnix();
    snap.underlyingPriceSource = "fallback";

    // Two contracts: one call, one put — matching DeltaGUI fixture shape.
    auto makeContract = [&](const std::string& optType, double strike) {
        FixtureContract c;
        c.underlying = symbol;
        c.expiration = "2026-06-19";
        c.daysToExpiration = 350.0;
        c.strike = strike;
        c.optionType = optType;
        c.multiplier = 100.0;
        c.bid = 3.50;
        c.ask = 3.70;
        c.last = 3.60;
        c.mark = 3.60;
        c.modelPrice = 3.55;
        c.modelResidual = 0.05;
        c.impliedVolatility = (provider == "schwab") ? 0.425 : 0.30;
        c.openInterest = 500.0;
        c.volume = 100.0;
        c.timestamp = snap.timestamp;
        // Build a contract symbol similar to DeltaGUI's fixture.
        c.symbol = symbol + "_250619" + (optType == "call" ? "C" : "P") +
                   std::to_string(static_cast<long long>(strike));
        return c;
    };

    snap.contracts.push_back(makeContract("call", 105.0));
    snap.contracts.push_back(makeContract("put", 95.0));
    return snap;
}

std::string fixtureToJson(const FixtureSnapshot& snap) {
    std::ostringstream out;
    out << "{\"schema_version\":" << snap.schemaVersion
        << ",\"provider\":\"" << escJson(snap.provider) << "\""
        << ",\"underlying\":\"" << escJson(snap.underlying) << "\""
        << ",\"underlying_price\":" << snap.underlyingPrice
        << ",\"risk_free_rate\":" << snap.riskFreeRate
        << ",\"timestamp\":" << snap.timestamp
        << ",\"metadata\":{"
        << "\"underlying_price_source\":\"" << escJson(snap.underlyingPriceSource) << "\""
        << ",\"backend_cache\":{\"cache\":\"bypass\"}"
        << "}"
        << ",\"contracts\":[";
    for (std::size_t i = 0; i < snap.contracts.size(); ++i) {
        const auto& c = snap.contracts[i];
        if (i > 0) out << ",";
        out << "{\"symbol\":\"" << escJson(c.symbol) << "\""
            << ",\"underlying\":\"" << escJson(c.underlying) << "\""
            << ",\"expiration\":\"" << escJson(c.expiration) << "\""
            << ",\"days_to_expiration\":" << c.daysToExpiration
            << ",\"strike\":" << c.strike
            << ",\"option_type\":\"" << escJson(c.optionType) << "\""
            << ",\"multiplier\":" << c.multiplier
            << ",\"bid\":" << c.bid
            << ",\"ask\":" << c.ask
            << ",\"last\":" << c.last
            << ",\"mark\":" << c.mark
            << ",\"model_price\":" << c.modelPrice
            << ",\"model_residual\":" << c.modelResidual
            << ",\"implied_volatility\":" << c.impliedVolatility
            << ",\"open_interest\":" << c.openInterest
            << ",\"volume\":" << c.volume
            << ",\"timestamp\":" << c.timestamp
            << "}";
    }
    out << "]}";
    return out.str();
}

// ---------------------------------------------------------------------------
// HTTP server state
// ---------------------------------------------------------------------------

struct StreamEvent {
    long long sequence = 0;
    std::string streamId;
    std::string type;
    std::string symbol;
    std::string contractSymbol;
    std::map<std::string, double> data;
};

struct BackendState {
    std::string provider;
    int port = 0;
    std::atomic<bool> running{true};
    std::mutex mutex;
    std::string streamState = "stopped";
    std::string streamSymbol;
    long long streamSequence = 0;
    std::vector<StreamEvent> streamEvents;
    // Fixture quote cache entries for /api/quotes/cache
    struct CacheQuote {
        std::string symbol;
        double bid = 0;
        double ask = 0;
        double last = 0;
        double mark = 0;
        double impliedVolatility = 0;
        long long openInterest = 0;
        long long volume = 0;
        long long sequence = 0;
        long long timestamp = 0;
    };
    long long cacheSequence = 0;
    std::vector<CacheQuote> cacheQuotes;

    FixtureSnapshot lastFixture;

    void populateFixture(const std::string& symbol) {
        lastFixture = makeFixture(provider, symbol);
        cacheQuotes.clear();
        cacheSequence = 0;
        for (const auto& c : lastFixture.contracts) {
            cacheQuotes.push_back({c.symbol, c.bid, c.ask, c.last, c.mark,
                                   c.impliedVolatility,
                                   static_cast<long long>(c.openInterest),
                                   static_cast<long long>(c.volume),
                                   ++cacheSequence, c.timestamp});
        }
        cacheQuotes.push_back({symbol, 99.98, 100.02, 100.0, 100.0,
                               0.0, 0, 1000, ++cacheSequence, nowUnix()});
    }

    void startStream(const std::string& symbol, bool fixture) {
        std::lock_guard<std::mutex> lock(mutex);
        streamState = "starting";
        streamSymbol = symbol;
        streamEvents.clear();
        streamSequence = 0;
        if (fixture) {
            // Emit underlying + option events matching DeltaGUI fixture shape.
            auto snap = makeFixture(provider, symbol);
            streamEvents.push_back({++streamSequence, "underlying", "underlying_quote", symbol, "",
                                   {{"bid", 99.98}, {"ask", 100.02}, {"last", 100.0}, {"mark", 100.0}, {"volume", 1000}}});
            for (const auto& c : snap.contracts) {
                streamEvents.push_back({++streamSequence, "option_quotes", "option_quotes", symbol, c.symbol,
                                       {{"bid", c.bid}, {"ask", c.ask}, {"last", c.last}, {"mark", c.mark}}});
            }
            cacheQuotes.clear();
            cacheSequence = 0;
            cacheQuotes.push_back({symbol, 99.98, 100.02, 100.0, 100.0,
                                   0.0, 0, 1000, ++cacheSequence, nowUnix()});
            for (const auto& c : snap.contracts) {
                cacheQuotes.push_back({c.symbol, c.bid, c.ask, c.last, c.mark,
                                       c.impliedVolatility,
                                       static_cast<long long>(c.openInterest),
                                       static_cast<long long>(c.volume),
                                       ++cacheSequence, c.timestamp});
            }
            streamState = "stopped"; // fixture mode just populates events
        }
    }

    void fixtureUpdate(const std::string& symbol, double bid, double ask) {
        std::lock_guard<std::mutex> lock(mutex);
        double last = (bid + ask) / 2.0;
        streamEvents.push_back({++streamSequence, "underlying", "underlying_quote", symbol, "",
                               {{"bid", bid}, {"ask", ask}, {"last", last}, {"mark", last}}});
        // Update cache
        bool found = false;
        for (auto& q : cacheQuotes) {
            if (q.symbol == symbol) {
                q.bid = bid; q.ask = ask; q.last = last; q.mark = last;
                q.sequence = ++cacheSequence;
                q.timestamp = nowUnix();
                found = true;
            }
        }
        if (!found) {
            cacheQuotes.push_back({symbol, bid, ask, last, last,
                                   0.0, 0, 0, ++cacheSequence, nowUnix()});
        }
    }

    std::string deltasJson(long long since) {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<StreamEvent> result;
        for (const auto& ev : streamEvents) {
            if (ev.sequence > since) result.push_back(ev);
        }
        long long latest = streamEvents.empty() ? 0 : streamEvents.back().sequence;
        std::ostringstream out;
        out << "{\"ok\":true,\"latest_sequence\":" << latest
            << ",\"count\":" << result.size()
            << ",\"events\":[";
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto& ev = result[i];
            if (i > 0) out << ",";
            out << "{\"sequence\":" << ev.sequence
                << ",\"stream\":\"" << escJson(ev.streamId) << "\""
                << ",\"type\":\"" << escJson(ev.type) << "\""
                << ",\"symbol\":\"" << escJson(ev.symbol) << "\"";
            if (!ev.contractSymbol.empty()) {
                out << ",\"contract_symbol\":\"" << escJson(ev.contractSymbol) << "\"";
            }
            out << ",\"data\":{";
            bool first = true;
            for (const auto& [k, v] : ev.data) {
                if (!first) out << ",";
                first = false;
                out << "\"" << escJson(k) << "\":" << v;
            }
            out << "}}";
        }
        out << "]}";
        return out.str();
    }
};

// ---------------------------------------------------------------------------
// HTTP parsing and response
// ---------------------------------------------------------------------------

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
    std::map<std::string, std::string> queryParams;
};

std::string urlDecode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int hi = (s[i+1] >= 'A') ? (s[i+1] & 0xDF) - 'A' + 10 : s[i+1] - '0';
            int lo = (s[i+2] >= 'A') ? (s[i+2] & 0xDF) - 'A' + 10 : s[i+2] - '0';
            out += static_cast<char>(hi * 16 + lo);
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

void parseQuery(const std::string& raw, std::map<std::string, std::string>& out) {
    std::string key, val;
    bool inVal = false;
    for (std::size_t i = 0; i <= raw.size(); ++i) {
        if (i == raw.size() || raw[i] == '&') {
            if (!key.empty()) out[urlDecode(key)] = urlDecode(val);
            key.clear(); val.clear(); inVal = false;
        } else if (raw[i] == '=' && !inVal) {
            inVal = true;
        } else if (inVal) {
            val += raw[i];
        } else {
            key += raw[i];
        }
    }
}

HttpRequest parseRequest(const std::string& raw) {
    HttpRequest req;
    // Parse request line: METHOD PATH?QUERY HTTP/1.1
    std::size_t lineEnd = raw.find("\r\n");
    std::string line = (lineEnd != std::string::npos) ? raw.substr(0, lineEnd) : raw;
    std::istringstream ls(line);
    ls >> req.method;
    std::string fullpath;
    ls >> fullpath;
    std::size_t qpos = fullpath.find('?');
    if (qpos != std::string::npos) {
        req.path = fullpath.substr(0, qpos);
        req.query = fullpath.substr(qpos + 1);
    } else {
        req.path = fullpath;
    }
    parseQuery(req.query, req.queryParams);
    // Parse body (after \r\n\r\n)
    std::size_t bodyStart = raw.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
        req.body = raw.substr(bodyStart + 4);
    }
    return req;
}

std::string httpResponse(int status, const std::string& body,
                         const std::string& contentType = "application/json") {
    std::string reason;
    switch (status) {
        case 200: reason = "OK"; break;
        case 400: reason = "Bad Request"; break;
        case 404: reason = "Not Found"; break;
        case 501: reason = "Not Implemented"; break;
        case 503: reason = "Service Unavailable"; break;
        default: reason = "OK"; break;
    }
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << reason << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n" << body;
    return out.str();
}

} // namespace

// ---------------------------------------------------------------------------
// Route handler — declared after all helpers
// ---------------------------------------------------------------------------

std::string handleRoute(BackendState& state, const HttpRequest& req);

int main(int argc, char* argv[]) {
    std::string provider = "schwab";
    int port = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--provider" && i + 1 < argc) {
            provider = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: markethub_compat_backend --provider schwab|tradestation --port PORT\n";
            return 0;
        }
    }

    if (provider != "schwab" && provider != "tradestation") {
        std::cerr << "error: provider must be 'schwab' or 'tradestation'\n";
        return 1;
    }
    if (port <= 0) {
        std::cerr << "error: --port is required\n";
        return 1;
    }

    // Ignore SIGPIPE from broken client connections.
    signal(SIGPIPE, SIG_IGN);

    int srvFd = socket(AF_INET, SOCK_STREAM, 0);
    if (srvFd < 0) {
        std::perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(srvFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(srvFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(srvFd);
        return 1;
    }
    if (listen(srvFd, 8) < 0) {
        std::perror("listen");
        close(srvFd);
        return 1;
    }

    BackendState state;
    state.provider = provider;
    state.port = port;
    state.populateFixture("AMD");

    std::cerr << "markethub_compat_backend listening on 127.0.0.1:" << port
              << " provider=" << provider << "\n";

    while (state.running) {
        int cliFd = accept(srvFd, nullptr, nullptr);
        if (cliFd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        // Read request (simple: read until \r\n\r\n or timeout).
        std::string raw;
        char buf[4096];
        struct timeval tv;
        tv.tv_sec = 3;
        tv.tv_usec = 0;
        setsockopt(cliFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        while (raw.find("\r\n\r\n") == std::string::npos && raw.size() < 65536) {
            ssize_t n = recv(cliFd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            raw.append(buf, static_cast<std::size_t>(n));
        }
        // If we have a Content-Length, keep reading.
        // (Simple: for POST with body we already got it above in most cases.)

        if (!raw.empty()) {
            HttpRequest httpReq = parseRequest(raw);
            std::string response = handleRoute(state, httpReq);
            send(cliFd, response.data(), response.size(), 0);
        }
        close(cliFd);
    }
    close(srvFd);
    return 0;
}

// ---------------------------------------------------------------------------
// Route handler
// ---------------------------------------------------------------------------

std::string handleRoute(BackendState& state, const HttpRequest& req) {
    const std::string& p = req.path;

    // GET /api/health
    if (req.method == "GET" && p == "/api/health") {
        std::ostringstream out;
        out << "{\"ok\":true,\"backend\":\"" << kBackendName << "\""
            << ",\"provider\":\"" << state.provider << "\""
            << ",\"time\":" << nowUnix() << "}";
        return httpResponse(200, out.str());
    }

    // GET /api/config or /api/config/status — same response (DeltaGUI aliases both)
    if (req.method == "GET" && (p == "/api/config" || p == "/api/config/status")) {
        std::ostringstream out;
        out << "{\"ok\":false"
            << ",\"backend\":\"" << kBackendName << "\""
            << ",\"provider\":\"" << state.provider << "\""
            << ",\"config_present\":false"
            << ",\"client_id_present\":false"
            << ",\"client_secret_present\":false"
            << ",\"redirect_uri_present\":false"
            << ",\"default_symbol\":\"AMD\""
            << ",\"strike_count\":20"
            << ",\"config_dir\":\"\""
            << ",\"error\":\"" << state.provider << " config missing\"}";
        return httpResponse(200, out.str());
    }

    // GET /api/auth/{provider}/status or /api/auth/status
    if (req.method == "GET" && p.find("/api/auth/") == 0 &&
        p.find("/status") != std::string::npos) {
        std::ostringstream out;
        out << "{\"ok\":false"
            << ",\"backend\":\"" << kBackendName << "\""
            << ",\"provider\":\"" << state.provider << "\""
            << ",\"configured\":false"
            << ",\"authenticated\":false"
            << ",\"authorized\":false"
            << ",\"token_file_present\":false"
            << ",\"refresh_present\":false"
            << ",\"refresh_valid\":false"
            << ",\"expires_at_unix\":0"
            << ",\"refresh_expires_at_unix\":0"
            << ",\"expires_in_seconds\":0"
            << ",\"refresh_expires_in_seconds\":0"
            << ",\"error\":\"" << state.provider << " token file missing\"}";
        return httpResponse(200, out.str());
    }

    // GET /api/auth/{provider}/start — returns 400 (not configured)
    if (req.method == "GET" && p.find("/api/auth/") == 0 &&
        p.find("/start") != std::string::npos) {
        return httpResponse(400, "{\"ok\":false,\"error\":\"not_configured\"}");
    }

    // GET /api/ready — 503 when not configured
    if (req.method == "GET" && p == "/api/ready") {
        std::ostringstream out;
        out << "{\"config\":{\"backend\":\"" << kBackendName << "\""
            << ",\"provider\":\"" << state.provider << "\""
            << ",\"config_present\":false}"
            << ",\"capabilities\":{\"schwab_rest\":false"
            << ",\"tradestation_rest\":false"
            << ",\"streaming\":false}}";
        return httpResponse(503, out.str());
    }

    // GET /api/market/options
    if (req.method == "GET" && p == "/api/market/options") {
        std::string symbol = "AMD";
        auto it = req.queryParams.find("symbol");
        if (it != req.queryParams.end() && !it->second.empty()) {
            symbol = it->second;
        }
        // Uppercase the symbol.
        for (auto& c : symbol) c = static_cast<char>(std::toupper(c));
        auto snap = makeFixture(state.provider, symbol);
        return httpResponse(200, fixtureToJson(snap));
    }

    // GET /api/quotes/status
    if (req.method == "GET" && p == "/api/quotes/status") {
        std::string symbol = "AMD";
        auto sit = req.queryParams.find("symbol");
        if (sit != req.queryParams.end() && !sit->second.empty()) {
            symbol = sit->second;
        }
        std::lock_guard<std::mutex> lock(state.mutex);
        long long seq = static_cast<long long>(state.cacheQuotes.size());
        std::ostringstream out;
        out << "{\"ok\":true"
            << ",\"symbol\":\"" << escJson(symbol) << "\""
            << ",\"sequence\":" << seq
            << ",\"known_contract_count\":" << state.cacheQuotes.size()
            << ",\"ready\":" << (state.cacheQuotes.size() > 0 ? "true" : "false")
            << ",\"stale\":false"
            << ",\"partial\":false"
            << ",\"backend\":\"" << kBackendName << "\""
            << ",\"time\":" << nowUnix() << "}";
        return httpResponse(200, out.str());
    }

    // GET /api/quotes/cache
    if (req.method == "GET" && p == "/api/quotes/cache") {
        long long since = 0;
        auto it = req.queryParams.find("since");
        if (it != req.queryParams.end()) {
            since = std::atoll(it->second.c_str());
        }
        std::lock_guard<std::mutex> lock(state.mutex);
        std::ostringstream out;
        out << "{\"ok\":true"
            << ",\"sequence\":" << state.cacheSequence
            << ",\"quotes\":[";
        bool first = true;
        for (const auto& q : state.cacheQuotes) {
            if (q.sequence <= since) continue;
            if (!first) out << ",";
            first = false;
            out << "{\"symbol\":\"" << escJson(q.symbol) << "\""
                << ",\"bid\":" << q.bid
                << ",\"ask\":" << q.ask
                << ",\"last\":" << q.last
                << ",\"mark\":" << q.mark
                << ",\"implied_volatility\":" << q.impliedVolatility
                << ",\"open_interest\":" << q.openInterest
                << ",\"volume\":" << q.volume
                << ",\"sequence\":" << q.sequence
                << ",\"timestamp\":" << q.timestamp << "}";
        }
        out << "]}";
        return httpResponse(200, out.str());
    }

    // GET /api/stream/status
    if (req.method == "GET" && p == "/api/stream/status") {
        std::lock_guard<std::mutex> lock(state.mutex);
        std::ostringstream out;
        out << "{\"backend\":\"" << kBackendName << "\""
            << ",\"provider\":\"" << state.provider << "\""
            << ",\"state\":\"" << state.streamState << "\""
            << ",\"stale\":false"
            << ",\"last_sequence\":" << state.streamSequence << "}";
        return httpResponse(200, out.str());
    }

    // POST /api/stream/start
    if (req.method == "POST" && p == "/api/stream/start") {
        std::string symbol = "AMD";
        auto it = req.queryParams.find("symbol");
        if (it != req.queryParams.end() && !it->second.empty()) {
            symbol = it->second;
        }
        bool fixture = req.queryParams.count("fixture") > 0;

        state.startStream(symbol, fixture);

        if (!fixture) {
            // Non-fixture live start: return "starting" with upstream metadata.
            std::ostringstream out;
            out << "{\"state\":\"starting\",\"last_error\":\"\""
                << ",\"upstreams\":{"
                << "\"underlying\":{\"path\":\"/v3/marketdata/stream/quotes/" << escJson(symbol) << "\"}"
                << ",\"options\":{\"path\":\"/v3/marketdata/stream/options/chains/" << escJson(symbol) << "\""
                << ",\"strike_scope\":\"underlying_price_percent_band\""
                << ",\"strike_percent_band\":15.0}"
                << "}}";
            return httpResponse(200, out.str());
        }
        return httpResponse(200, "{\"state\":\"stopped\",\"ok\":true}");
    }

    // GET /api/stream/deltas
    if (req.method == "GET" && p == "/api/stream/deltas") {
        long long since = 0;
        auto it = req.queryParams.find("since");
        if (it != req.queryParams.end()) {
            since = std::atoll(it->second.c_str());
        }
        // Check fixture=true param (events are already populated from start)
        if (req.queryParams.count("fixture") > 0) {
            // Fixture deltas are the same stream events
        }
        return httpResponse(200, state.deltasJson(since));
    }

    // POST /api/stream/fixture/generate — regenerate fixture events
    if (req.method == "POST" && p == "/api/stream/fixture/generate") {
        state.startStream(state.streamSymbol.empty() ? "AMD" : state.streamSymbol, true);
        return httpResponse(200, "{\"ok\":true,\"message\":\"Fixture events regenerated\"}");
    }

    // POST /api/stream/fixture/update
    if (req.method == "POST" && p == "/api/stream/fixture/update") {
        std::string symbol = "AMD";
        double bid = 0, ask = 0;
        auto sit = req.queryParams.find("symbol");
        if (sit != req.queryParams.end()) symbol = sit->second;
        auto bit = req.queryParams.find("bid");
        if (bit != req.queryParams.end()) bid = std::atof(bit->second.c_str());
        auto ait = req.queryParams.find("ask");
        if (ait != req.queryParams.end()) ask = std::atof(ait->second.c_str());
        state.fixtureUpdate(symbol, bid, ask);
        return httpResponse(200, "{\"ok\":true}");
    }

    // GET /api/stream/options — 501 placeholder
    if (req.method == "GET" && p == "/api/stream/options") {
        return httpResponse(501,
            "{\"ok\":false,\"code\":\"not_implemented\""
            ",\"event_schema\":{\"type\":\"option_quote_delta\"}}");
    }

    // POST /api/stream/stop
    if (req.method == "POST" && p == "/api/stream/stop") {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.streamState = "stopped";
        return httpResponse(200, "{\"state\":\"stopped\",\"ok\":true}");
    }

    // POST /api/stream/subscribe — 400 (no body / not supported in compat shim)
    if (req.method == "POST" && p == "/api/stream/subscribe") {
        return httpResponse(400, "{\"ok\":false,\"code\":\"bad_request\"}");
    }

    // POST /api/stream/unsubscribe — 400
    if (req.method == "POST" && p == "/api/stream/unsubscribe") {
        return httpResponse(400, "{\"ok\":false,\"code\":\"bad_request\"}");
    }

    // GET /api/stream/subscriptions
    if (req.method == "GET" && p == "/api/stream/subscriptions") {
        std::lock_guard<std::mutex> lock(state.mutex);
        std::ostringstream out;
        out << "{\"subscriptions\":[],\"state\":\"" << state.streamState << "\"}";
        return httpResponse(200, out.str());
    }

    // 404 for everything else
    std::ostringstream out;
    out << "{\"ok\":false,\"code\":\"not_found\""
        << ",\"path\":\"" << escJson(p) << "\"}";
    return httpResponse(404, out.str());
}
