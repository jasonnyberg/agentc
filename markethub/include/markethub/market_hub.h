// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// AgentC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with AgentC. If not, see <https://www.gnu.org/licenses/>.

// G117 Market Data Hub — pub/sub hub with quote cache, bounded history,
// and a machine-testable highlight engine.
//
// Kept separate from core AgentC machinery by design: the hub consumes
// AgentC facilities (its events convert to worker/TCC-style JSON envelopes
// at the bridge boundary) but is never linked into libedict or the VM.

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "markethub/market_event.h"

namespace agentc::markethub {

// ---------------------------------------------------------------------------
// Subscription filter — declarative, WP_G117 §3.
// ---------------------------------------------------------------------------

struct SubscriptionFilter {
    // Empty = all event types.
    std::vector<EventType> types;
    // Empty = all symbols. Matched against MarketEvent::symbol (uppercase).
    std::vector<std::string> symbols;

    bool matches(const MarketEvent& event) const;
};

// ---------------------------------------------------------------------------
// History policy + store — WP_G117 §4.
// ---------------------------------------------------------------------------

struct HistoryPolicy {
    // Bucket width for downsampling. Last write within a bucket wins.
    long long resolutionSeconds = 60;
    // Per-series cap; oldest samples drop first.
    std::size_t maxSamples = 1000;
};

struct HistorySample {
    long long bucketUnix = 0;
    double value = 0.0;
    long long sourceSequence = 0;
};

// Downsampled per-field history. Series key: symbol|contract|field.
class HistoryStore {
public:
    explicit HistoryStore(HistoryPolicy policy = {});

    void record(const MarketEvent& event);

    // Samples for one series, oldest first. Empty when unknown.
    std::vector<HistorySample> series(const std::string& symbol,
                                      const std::string& contract,
                                      const std::string& field) const;

    std::size_t seriesCount() const;
    const HistoryPolicy& policy() const { return policy_; }

private:
    HistoryPolicy policy_;
    std::map<std::string, std::deque<HistorySample>> series_;
};

// ---------------------------------------------------------------------------
// Current quote cache — latest event per (symbol, contract, type).
// ---------------------------------------------------------------------------

class QuoteCache {
public:
    void update(const MarketEvent& event);

    // Latest event for the key; false when absent.
    bool latest(EventType type, const std::string& symbol,
                const std::string& contract, MarketEvent& out) const;

    // Copy of all cached latest events (for rule evaluation sweeps).
    std::vector<MarketEvent> snapshot() const;

    std::size_t size() const;

private:
    std::map<std::string, MarketEvent> latest_;
};

// ---------------------------------------------------------------------------
// Highlight rules — WP_G117 §2 `highlight` events, machine-testable.
// ---------------------------------------------------------------------------

// Publishes highlight when a numeric field moves more than thresholdPct
// within lookbackSeconds (evaluated against current cache + history).
struct PriceMoveRule {
    std::string ruleId = "price_move";
    EventType sourceType = EventType::UnderlyingQuote;
    std::string field = "last";
    double thresholdPct = 1.0;
    long long lookbackSeconds = 300;
};

// Publishes highlight when the freshest event for a tracked entity is older
// than freshnessSeconds at evaluation time.
struct StaleQuoteRule {
    std::string ruleId = "stale_quote";
    long long freshnessSeconds = 30;
};

// ---------------------------------------------------------------------------
// Subscribers — the two first-class shapes from WP_G117 §3.
// ---------------------------------------------------------------------------

class Subscriber {
public:
    virtual ~Subscriber() = default;
    virtual void onEvent(const MarketEvent& event) = 0;
};

// DeltaGUI-shaped consumer: bounded replay buffer queried by sequence,
// mirroring the /api/stream/deltas since-sequence contract.
class DeltaFeedSubscriber : public Subscriber {
public:
    explicit DeltaFeedSubscriber(std::size_t maxEvents = 5000);

    void onEvent(const MarketEvent& event) override;

    // {"ok":true,"latest_sequence":N,"count":K,"events":[...]}
    std::string deltasSinceJson(long long sinceSequence) const;
    std::vector<MarketEvent> deltasSince(long long sinceSequence) const;
    long long latestSequence() const;

private:
    std::size_t maxEvents_;
    std::deque<MarketEvent> events_;
};

// AgentC/Edict-shaped consumer: accumulates events and drains them as a
// structured envelope, matching worker/TCC envelope conventions.
class EnvelopeCollectorSubscriber : public Subscriber {
public:
    void onEvent(const MarketEvent& event) override;

    // Drains accumulated events:
    // {"ok":true,"status":"collected","count":K,"events":[...]}
    std::string collectJson();
    std::size_t pendingCount() const { return pending_.size(); }

private:
    std::vector<MarketEvent> pending_;
};

// ---------------------------------------------------------------------------
// MarketHub — the bus.
// ---------------------------------------------------------------------------

class MarketHub {
public:
    using Clock = std::function<long long()>; // unix seconds

    explicit MarketHub(HistoryPolicy historyPolicy = {}, Clock clock = {});

    // --- Subscriptions -----------------------------------------------------
    // Returns a subscription id. The hub does not own the subscriber.
    int subscribe(Subscriber* subscriber, SubscriptionFilter filter = {});
    bool unsubscribe(int subscriptionId);
    std::size_t subscriberCount() const;

    // --- Highlight rules ---------------------------------------------------
    void addPriceMoveRule(PriceMoveRule rule);
    void addStaleQuoteRule(StaleQuoteRule rule);

    // Evaluates stale-quote rules against the cache "now"; publishes one
    // highlight per newly-stale entity. Returns highlights published.
    std::size_t evaluateStaleness();

    // --- Ingest ------------------------------------------------------------
    // Stamps sequence/receivedUnix, updates cache + history, fans out to
    // matching subscribers, then runs price-move rules (which may publish
    // derived highlight events back onto the same bus).
    long long publish(MarketEvent event);

    // Deterministic fixture ingest for provider-free validation (WP_G117 §1):
    // an underlying_quote plus `contractCount` option_quote events per tick.
    // Returns number of events published.
    std::size_t ingestFixtureTick(const std::string& symbol,
                                  double basePrice,
                                  int tick,
                                  int contractCount = 2);

    // --- Reads -------------------------------------------------------------
    const QuoteCache& cache() const { return cache_; }
    const HistoryStore& history() const { return history_; }
    long long currentSequence() const;

    // {"ok":true,"symbol":...,"field":...,"count":K,"samples":[...]}
    std::string historyWindowJson(const std::string& symbol,
                                  const std::string& contract,
                                  const std::string& field) const;

private:
    struct Subscription {
        int id = 0;
        Subscriber* subscriber = nullptr;
        SubscriptionFilter filter;
    };

    void dispatch(const MarketEvent& event);
    std::vector<MarketEvent> staleHighlightsLocked(long long now);
    long long nowUnix() const;

    mutable std::mutex mutex_;
    Clock clock_;
    long long sequence_ = 0;
    int nextSubscriptionId_ = 1;
    std::vector<Subscription> subscriptions_;
    QuoteCache cache_;
    HistoryStore history_;
    std::vector<PriceMoveRule> priceMoveRules_;
    std::vector<StaleQuoteRule> staleQuoteRules_;
    // rule_id|symbol|contract -> last highlighted state marker
    std::map<std::string, long long> staleAlreadyFlagged_;
};

} // namespace agentc::markethub
