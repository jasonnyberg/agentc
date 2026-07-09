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

#include "markethub/market_hub.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>

namespace agentc::markethub {

namespace {

std::string cacheKey(EventType type, const std::string& symbol,
                     const std::string& contract) {
    std::string key = eventTypeName(type);
    key += '|';
    key += symbol;
    key += '|';
    key += contract;
    return key;
}

std::string seriesKey(const std::string& symbol, const std::string& contract,
                      const std::string& field) {
    std::string key = symbol;
    key += '|';
    key += contract;
    key += '|';
    key += field;
    return key;
}

std::string eventsArrayJson(const std::vector<MarketEvent>& events) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (i > 0) out << ",";
        out << events[i].toJson();
    }
    out << "]";
    return out.str();
}

} // namespace

// ---------------------------------------------------------------------------
// SubscriptionFilter
// ---------------------------------------------------------------------------

bool SubscriptionFilter::matches(const MarketEvent& event) const {
    if (!types.empty() &&
        std::find(types.begin(), types.end(), event.type) == types.end()) {
        return false;
    }
    if (!symbols.empty() &&
        std::find(symbols.begin(), symbols.end(), event.symbol) == symbols.end()) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// HistoryStore
// ---------------------------------------------------------------------------

HistoryStore::HistoryStore(HistoryPolicy policy) : policy_(policy) {
    if (policy_.resolutionSeconds < 1) policy_.resolutionSeconds = 1;
    if (policy_.maxSamples < 1) policy_.maxSamples = 1;
}

void HistoryStore::record(const MarketEvent& event) {
    // Only quote-shaped events feed history series.
    if (event.type != EventType::UnderlyingQuote &&
        event.type != EventType::OptionQuote) {
        return;
    }
    const long long bucket =
        (event.freshnessUnix() / policy_.resolutionSeconds) * policy_.resolutionSeconds;
    for (const auto& [field, value] : event.num) {
        auto& series = series_[seriesKey(event.symbol, event.contract, field)];
        if (!series.empty() && series.back().bucketUnix == bucket) {
            // Last write within a bucket wins.
            series.back().value = value;
            series.back().sourceSequence = event.sequence;
            continue;
        }
        series.push_back(HistorySample{bucket, value, event.sequence});
        while (series.size() > policy_.maxSamples) {
            series.pop_front();
        }
    }
}

std::vector<HistorySample> HistoryStore::series(const std::string& symbol,
                                                const std::string& contract,
                                                const std::string& field) const {
    auto it = series_.find(seriesKey(symbol, contract, field));
    if (it == series_.end()) {
        return {};
    }
    return std::vector<HistorySample>(it->second.begin(), it->second.end());
}

std::size_t HistoryStore::seriesCount() const {
    return series_.size();
}

// ---------------------------------------------------------------------------
// QuoteCache
// ---------------------------------------------------------------------------

void QuoteCache::update(const MarketEvent& event) {
    latest_[cacheKey(event.type, event.symbol, event.contract)] = event;
}

bool QuoteCache::latest(EventType type, const std::string& symbol,
                        const std::string& contract, MarketEvent& out) const {
    auto it = latest_.find(cacheKey(type, symbol, contract));
    if (it == latest_.end()) {
        return false;
    }
    out = it->second;
    return true;
}

std::vector<MarketEvent> QuoteCache::snapshot() const {
    std::vector<MarketEvent> result;
    result.reserve(latest_.size());
    for (const auto& [key, event] : latest_) {
        result.push_back(event);
    }
    return result;
}

std::size_t QuoteCache::size() const {
    return latest_.size();
}

// ---------------------------------------------------------------------------
// DeltaFeedSubscriber
// ---------------------------------------------------------------------------

DeltaFeedSubscriber::DeltaFeedSubscriber(std::size_t maxEvents)
    : maxEvents_(maxEvents == 0 ? 1 : maxEvents) {}

void DeltaFeedSubscriber::onEvent(const MarketEvent& event) {
    events_.push_back(event);
    while (events_.size() > maxEvents_) {
        events_.pop_front();
    }
}

std::vector<MarketEvent> DeltaFeedSubscriber::deltasSince(long long sinceSequence) const {
    std::vector<MarketEvent> result;
    for (const auto& event : events_) {
        if (event.sequence > sinceSequence) {
            result.push_back(event);
        }
    }
    return result;
}

long long DeltaFeedSubscriber::latestSequence() const {
    return events_.empty() ? 0 : events_.back().sequence;
}

std::string DeltaFeedSubscriber::deltasSinceJson(long long sinceSequence) const {
    auto events = deltasSince(sinceSequence);
    std::ostringstream out;
    out << "{\"ok\":true,\"latest_sequence\":" << latestSequence()
        << ",\"count\":" << events.size()
        << ",\"events\":" << eventsArrayJson(events) << "}";
    return out.str();
}

// ---------------------------------------------------------------------------
// EnvelopeCollectorSubscriber
// ---------------------------------------------------------------------------

void EnvelopeCollectorSubscriber::onEvent(const MarketEvent& event) {
    pending_.push_back(event);
}

std::string EnvelopeCollectorSubscriber::collectJson() {
    std::ostringstream out;
    out << "{\"ok\":true,\"status\":\"collected\",\"count\":" << pending_.size()
        << ",\"events\":" << eventsArrayJson(pending_) << "}";
    pending_.clear();
    return out.str();
}

// ---------------------------------------------------------------------------
// MarketHub
// ---------------------------------------------------------------------------

MarketHub::MarketHub(HistoryPolicy historyPolicy, Clock clock)
    : clock_(std::move(clock)), history_(historyPolicy) {}

long long MarketHub::nowUnix() const {
    if (clock_) {
        return clock_();
    }
    return static_cast<long long>(std::time(nullptr));
}

int MarketHub::subscribe(Subscriber* subscriber, SubscriptionFilter filter) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int id = nextSubscriptionId_++;
    subscriptions_.push_back(Subscription{id, subscriber, std::move(filter)});
    return id;
}

bool MarketHub::unsubscribe(int subscriptionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
                           [subscriptionId](const Subscription& sub) {
                               return sub.id == subscriptionId;
                           });
    if (it == subscriptions_.end()) {
        return false;
    }
    subscriptions_.erase(it);
    return true;
}

std::size_t MarketHub::subscriberCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return subscriptions_.size();
}

void MarketHub::addPriceMoveRule(PriceMoveRule rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    priceMoveRules_.push_back(std::move(rule));
}

void MarketHub::addStaleQuoteRule(StaleQuoteRule rule) {
    std::lock_guard<std::mutex> lock(mutex_);
    staleQuoteRules_.push_back(std::move(rule));
}

void MarketHub::dispatch(const MarketEvent& event) {
    // Called with mutex_ held. Snapshot matching subscribers so a subscriber
    // callback can't invalidate iteration mid-fanout.
    std::vector<Subscriber*> targets;
    targets.reserve(subscriptions_.size());
    for (const auto& sub : subscriptions_) {
        if (sub.subscriber && sub.filter.matches(event)) {
            targets.push_back(sub.subscriber);
        }
    }
    for (Subscriber* target : targets) {
        target->onEvent(event);
    }
}

long long MarketHub::publish(MarketEvent event) {
    std::vector<MarketEvent> highlights;
    long long stampedSequence = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        event.sequence = ++sequence_;
        stampedSequence = event.sequence;
        if (event.receivedUnix == 0) {
            event.receivedUnix = nowUnix();
        }
        cache_.update(event);
        history_.record(event);
        dispatch(event);

        // Price-move rules read history that now includes this event.
        if (event.type != EventType::Highlight) {
            for (const auto& rule : priceMoveRules_) {
                if (event.type != rule.sourceType || !event.hasNum(rule.field)) {
                    continue;
                }
                const auto samples =
                    history_.series(event.symbol, event.contract, rule.field);
                if (samples.size() < 2) {
                    continue;
                }
                const auto& latest = samples.back();
                const long long windowStart = latest.bucketUnix - rule.lookbackSeconds;
                // Oldest retained sample inside the lookback window.
                const HistorySample* baseline = nullptr;
                for (const auto& sample : samples) {
                    if (sample.bucketUnix >= windowStart &&
                        sample.sourceSequence != latest.sourceSequence) {
                        baseline = &sample;
                        break;
                    }
                }
                if (!baseline || baseline->value == 0.0) {
                    continue;
                }
                const double changePct =
                    (latest.value - baseline->value) / baseline->value * 100.0;
                if (std::fabs(changePct) < rule.thresholdPct) {
                    continue;
                }
                MarketEvent highlight;
                highlight.type = EventType::Highlight;
                highlight.symbol = event.symbol;
                highlight.contract = event.contract;
                highlight.source = "highlight:" + rule.ruleId;
                highlight.observedUnix = event.freshnessUnix();
                highlight.text["rule_id"] = rule.ruleId;
                highlight.text["kind"] = "price_move";
                highlight.text["detail"] = rule.field;
                highlight.num["change_pct"] = changePct;
                highlight.num["from"] = baseline->value;
                highlight.num["to"] = latest.value;
                highlight.num["window_seconds"] =
                    static_cast<double>(rule.lookbackSeconds);
                highlights.push_back(std::move(highlight));
            }
        }
    }
    // Re-publish derived highlights onto the same bus (outside the lock;
    // publish() re-acquires it). WP_G117 §2: uniform stream for subscribers.
    for (auto& highlight : highlights) {
        publish(std::move(highlight));
    }
    return stampedSequence;
}

std::size_t MarketHub::evaluateStaleness() {
    std::vector<MarketEvent> highlights;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        highlights = staleHighlightsLocked(nowUnix());
    }
    for (auto& highlight : highlights) {
        publish(std::move(highlight));
    }
    return highlights.size();
}

std::vector<MarketEvent> MarketHub::staleHighlightsLocked(long long now) {
    std::vector<MarketEvent> highlights;
    for (const auto& rule : staleQuoteRules_) {
        for (const auto& cached : cache_.snapshot()) {
            if (cached.type != EventType::UnderlyingQuote &&
                cached.type != EventType::OptionQuote) {
                continue;
            }
            const long long age = now - cached.freshnessUnix();
            const std::string flagKey =
                rule.ruleId + "|" + cached.symbol + "|" + cached.contract;
            if (age <= rule.freshnessSeconds) {
                // Entity is fresh again; allow future re-flagging.
                staleAlreadyFlagged_.erase(flagKey);
                continue;
            }
            // Only flag each stale transition once (keyed by the event that
            // went stale) so repeated sweeps don't spam duplicates.
            auto it = staleAlreadyFlagged_.find(flagKey);
            if (it != staleAlreadyFlagged_.end() &&
                it->second == cached.sequence) {
                continue;
            }
            staleAlreadyFlagged_[flagKey] = cached.sequence;

            MarketEvent highlight;
            highlight.type = EventType::Highlight;
            highlight.symbol = cached.symbol;
            highlight.contract = cached.contract;
            highlight.source = "highlight:" + rule.ruleId;
            highlight.observedUnix = now;
            highlight.text["rule_id"] = rule.ruleId;
            highlight.text["kind"] = "stale_quote";
            highlight.text["detail"] = eventTypeName(cached.type);
            highlight.num["age_seconds"] = static_cast<double>(age);
            highlight.num["stale_sequence"] =
                static_cast<double>(cached.sequence);
            highlights.push_back(std::move(highlight));
        }
    }
    return highlights;
}

long long MarketHub::currentSequence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sequence_;
}

std::string MarketHub::historyWindowJson(const std::string& symbol,
                                         const std::string& contract,
                                         const std::string& field) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto samples = history_.series(symbol, contract, field);
    std::ostringstream out;
    out << "{\"ok\":true,\"symbol\":\"" << symbol
        << "\",\"contract\":\"" << contract
        << "\",\"field\":\"" << field
        << "\",\"resolution_seconds\":" << history_.policy().resolutionSeconds
        << ",\"count\":" << samples.size() << ",\"samples\":[";
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (i > 0) out << ",";
        out << "{\"bucket_unix\":" << samples[i].bucketUnix
            << ",\"value\":" << samples[i].value
            << ",\"source_sequence\":" << samples[i].sourceSequence << "}";
    }
    out << "]}";
    return out.str();
}

std::size_t MarketHub::ingestFixtureTick(const std::string& symbol,
                                         double basePrice,
                                         int tick,
                                         int contractCount) {
    std::size_t published = 0;
    const long long observed = nowUnix();
    // Deterministic drift: ±0.5% oscillation plus slow trend.
    const double drift = basePrice * (0.005 * std::sin(tick * 0.7) +
                                      0.001 * tick);
    const double last = basePrice + drift;

    MarketEvent underlying;
    underlying.type = EventType::UnderlyingQuote;
    underlying.symbol = symbol;
    underlying.source = "fixture";
    underlying.observedUnix = observed;
    underlying.num["bid"] = last - 0.02;
    underlying.num["ask"] = last + 0.02;
    underlying.num["last"] = last;
    underlying.num["mark"] = last;
    underlying.num["volume"] = 1000.0 + 10.0 * tick;
    publish(std::move(underlying));
    ++published;

    for (int i = 0; i < contractCount; ++i) {
        const bool isCall = (i % 2) == 0;
        const double strike = std::round(basePrice) + (isCall ? 5.0 : -5.0) * (1 + i / 2);
        std::ostringstream contract;
        contract << symbol << "_2506" << (isCall ? "C" : "P")
                 << static_cast<long long>(strike);
        const double optMark = std::max(0.05, (isCall ? last - strike : strike - last) * 0.4 + 1.5);

        MarketEvent option;
        option.type = EventType::OptionQuote;
        option.symbol = symbol;
        option.contract = contract.str();
        option.source = "fixture";
        option.observedUnix = observed;
        option.num["bid"] = std::max(0.0, optMark - 0.05);
        option.num["ask"] = optMark + 0.05;
        option.num["mark"] = optMark;
        option.num["last"] = optMark;
        option.num["implied_volatility"] = 0.30 + 0.01 * (tick % 5);
        option.num["open_interest"] = 500.0 + 25.0 * i;
        option.num["volume"] = 100.0 + 5.0 * tick;
        publish(std::move(option));
        ++published;
    }
    return published;
}

} // namespace agentc::markethub
