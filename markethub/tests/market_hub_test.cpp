// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

// G117 hub MVP + highlight engine coverage (Phases 2 and 3).

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "markethub/market_hub.h"

using namespace agentc::markethub;

namespace {

// Deterministic manual clock for machine-testable freshness rules.
struct ManualClock {
    long long now = 1000000;
    MarketHub::Clock fn() {
        return [this]() { return now; };
    }
};

MarketEvent underlyingQuote(const std::string& symbol, double last,
                            long long observed) {
    MarketEvent event;
    event.type = EventType::UnderlyingQuote;
    event.symbol = symbol;
    event.source = "fixture";
    event.observedUnix = observed;
    event.num["last"] = last;
    event.num["bid"] = last - 0.02;
    event.num["ask"] = last + 0.02;
    return event;
}

} // namespace

TEST(MarketEventTest, TypeNamesRoundTrip) {
    for (EventType type : {EventType::UnderlyingQuote, EventType::OptionQuote,
                           EventType::OptionChainSnapshot,
                           EventType::HistorySample, EventType::Highlight}) {
        EventType parsed;
        ASSERT_TRUE(parseEventType(eventTypeName(type), parsed));
        EXPECT_EQ(parsed, type);
    }
    EventType ignored;
    EXPECT_FALSE(parseEventType("nonsense", ignored));
}

TEST(MarketEventTest, ToJsonCarriesEnvelopeAndPayload) {
    MarketEvent event = underlyingQuote("AMD", 120.5, 42);
    event.sequence = 7;
    event.receivedUnix = 43;
    event.text["note"] = "hello \"world\"";
    const std::string json = event.toJson();
    EXPECT_NE(json.find("\"type\":\"underlying_quote\""), std::string::npos);
    EXPECT_NE(json.find("\"symbol\":\"AMD\""), std::string::npos);
    EXPECT_NE(json.find("\"sequence\":7"), std::string::npos);
    EXPECT_NE(json.find("\"last\":120.5"), std::string::npos);
    EXPECT_NE(json.find("\\\"world\\\""), std::string::npos);
}

TEST(MarketHubTest, PublishStampsMonotonicSequenceAndFansOutByFilter) {
    ManualClock clock;
    MarketHub hub({}, clock.fn());

    DeltaFeedSubscriber deltaFeed;
    EnvelopeCollectorSubscriber collector;

    SubscriptionFilter amdOnly;
    amdOnly.symbols = {"AMD"};
    hub.subscribe(&deltaFeed, amdOnly);

    SubscriptionFilter optionsOnly;
    optionsOnly.types = {EventType::OptionQuote};
    hub.subscribe(&collector, optionsOnly);

    EXPECT_EQ(hub.subscriberCount(), 2u);

    const long long seq1 = hub.publish(underlyingQuote("AMD", 100.0, clock.now));
    const long long seq2 = hub.publish(underlyingQuote("NVDA", 500.0, clock.now));

    MarketEvent option;
    option.type = EventType::OptionQuote;
    option.symbol = "AMD";
    option.contract = "AMD_2506C105";
    option.source = "fixture";
    option.observedUnix = clock.now;
    option.num["mark"] = 2.5;
    const long long seq3 = hub.publish(option);

    EXPECT_LT(seq1, seq2);
    EXPECT_LT(seq2, seq3);

    // Delta feed got only AMD events (underlying + option).
    const auto deltas = deltaFeed.deltasSince(0);
    ASSERT_EQ(deltas.size(), 2u);
    EXPECT_EQ(deltas[0].symbol, "AMD");
    EXPECT_EQ(deltas[1].contract, "AMD_2506C105");

    // Collector got only option_quote events.
    EXPECT_EQ(collector.pendingCount(), 1u);
    const std::string envelope = collector.collectJson();
    EXPECT_NE(envelope.find("\"status\":\"collected\""), std::string::npos);
    EXPECT_NE(envelope.find("\"count\":1"), std::string::npos);
    EXPECT_EQ(collector.pendingCount(), 0u);
}

TEST(MarketHubTest, QuoteCacheKeepsLatestPerEntity) {
    ManualClock clock;
    MarketHub hub({}, clock.fn());

    hub.publish(underlyingQuote("AMD", 100.0, clock.now));
    clock.now += 5;
    hub.publish(underlyingQuote("AMD", 101.5, clock.now));

    MarketEvent cached;
    ASSERT_TRUE(hub.cache().latest(EventType::UnderlyingQuote, "AMD", "", cached));
    EXPECT_DOUBLE_EQ(cached.numOr("last", 0.0), 101.5);
    EXPECT_EQ(hub.cache().size(), 1u);
}

TEST(MarketHubTest, HistoryDownsamplesToBucketsAndBoundsSamples) {
    ManualClock clock;
    HistoryPolicy policy;
    policy.resolutionSeconds = 60;
    policy.maxSamples = 3;
    MarketHub hub(policy, clock.fn());

    // Two updates within one bucket: last write wins.
    hub.publish(underlyingQuote("AMD", 100.0, 1000020));
    hub.publish(underlyingQuote("AMD", 100.5, 1000050));
    // Three more buckets; cap of 3 drops the oldest.
    hub.publish(underlyingQuote("AMD", 101.0, 1000080));
    hub.publish(underlyingQuote("AMD", 102.0, 1000140));
    hub.publish(underlyingQuote("AMD", 103.0, 1000200));

    const auto samples = hub.history().series("AMD", "", "last");
    ASSERT_EQ(samples.size(), 3u);
    EXPECT_EQ(samples[0].bucketUnix, 1000080);
    EXPECT_DOUBLE_EQ(samples[0].value, 101.0);
    EXPECT_EQ(samples[2].bucketUnix, 1000200);
    EXPECT_DOUBLE_EQ(samples[2].value, 103.0);

    const std::string window = hub.historyWindowJson("AMD", "", "last");
    EXPECT_NE(window.find("\"resolution_seconds\":60"), std::string::npos);
    EXPECT_NE(window.find("\"count\":3"), std::string::npos);
}

TEST(MarketHubTest, PriceMoveRulePublishesHighlightOntoBus) {
    ManualClock clock;
    HistoryPolicy policy;
    policy.resolutionSeconds = 60;
    MarketHub hub(policy, clock.fn());

    PriceMoveRule rule;
    rule.thresholdPct = 2.0;
    rule.lookbackSeconds = 600;
    hub.addPriceMoveRule(rule);

    EnvelopeCollectorSubscriber highlightsOnly;
    SubscriptionFilter filter;
    filter.types = {EventType::Highlight};
    hub.subscribe(&highlightsOnly, filter);

    hub.publish(underlyingQuote("AMD", 100.0, 1000020));
    // +1% in next bucket: below threshold, no highlight.
    hub.publish(underlyingQuote("AMD", 101.0, 1000080));
    EXPECT_EQ(highlightsOnly.pendingCount(), 0u);

    // +3% versus baseline: above threshold -> highlight republished on bus.
    hub.publish(underlyingQuote("AMD", 103.0, 1000140));
    ASSERT_EQ(highlightsOnly.pendingCount(), 1u);

    const std::string envelope = highlightsOnly.collectJson();
    EXPECT_NE(envelope.find("\"rule_id\":\"price_move\""), std::string::npos);
    EXPECT_NE(envelope.find("\"kind\":\"price_move\""), std::string::npos);
    EXPECT_NE(envelope.find("\"source\":\"highlight:price_move\""), std::string::npos);
    EXPECT_NE(envelope.find("change_pct"), std::string::npos);
}

TEST(MarketHubTest, StaleQuoteRuleFlagsOncePerStaleTransition) {
    ManualClock clock;
    MarketHub hub({}, clock.fn());

    StaleQuoteRule rule;
    rule.freshnessSeconds = 30;
    hub.addStaleQuoteRule(rule);

    EnvelopeCollectorSubscriber highlights;
    SubscriptionFilter filter;
    filter.types = {EventType::Highlight};
    hub.subscribe(&highlights, filter);

    hub.publish(underlyingQuote("AMD", 100.0, clock.now));

    // Fresh: no highlight.
    clock.now += 10;
    EXPECT_EQ(hub.evaluateStaleness(), 0u);

    // Stale: exactly one highlight.
    clock.now += 60;
    EXPECT_EQ(hub.evaluateStaleness(), 1u);
    // Re-evaluating without new data does not duplicate.
    EXPECT_EQ(hub.evaluateStaleness(), 0u);
    ASSERT_EQ(highlights.pendingCount(), 1u);

    const std::string envelope = highlights.collectJson();
    EXPECT_NE(envelope.find("\"kind\":\"stale_quote\""), std::string::npos);
    EXPECT_NE(envelope.find("age_seconds"), std::string::npos);

    // Fresh data arrives, then goes stale again -> flags once more.
    hub.publish(underlyingQuote("AMD", 100.5, clock.now));
    EXPECT_EQ(hub.evaluateStaleness(), 0u);
    clock.now += 60;
    EXPECT_EQ(hub.evaluateStaleness(), 1u);
}

TEST(MarketHubTest, FixtureIngestFeedsCacheHistoryAndBothSubscriberShapes) {
    ManualClock clock;
    HistoryPolicy policy;
    policy.resolutionSeconds = 1;
    MarketHub hub(policy, clock.fn());

    DeltaFeedSubscriber deltaFeed;
    EnvelopeCollectorSubscriber collector;
    hub.subscribe(&deltaFeed);
    hub.subscribe(&collector);

    std::size_t total = 0;
    for (int tick = 0; tick < 3; ++tick) {
        total += hub.ingestFixtureTick("AMD", 100.0, tick, 2);
        clock.now += 1;
    }
    // 1 underlying + 2 options per tick.
    EXPECT_EQ(total, 9u);
    EXPECT_EQ(hub.currentSequence(), 9);

    // Cache: underlying + 2 distinct contracts.
    EXPECT_EQ(hub.cache().size(), 3u);

    // History: series exist for underlying "last".
    EXPECT_FALSE(hub.history().series("AMD", "", "last").empty());

    // DeltaGUI-shaped replay: since-sequence semantics.
    const auto all = deltaFeed.deltasSince(0);
    EXPECT_EQ(all.size(), 9u);
    const auto tail = deltaFeed.deltasSince(6);
    EXPECT_EQ(tail.size(), 3u);
    const std::string deltasJson = deltaFeed.deltasSinceJson(6);
    EXPECT_NE(deltasJson.find("\"latest_sequence\":9"), std::string::npos);
    EXPECT_NE(deltasJson.find("\"count\":3"), std::string::npos);

    // AgentC-shaped envelope drain.
    const std::string envelope = collector.collectJson();
    EXPECT_NE(envelope.find("\"count\":9"), std::string::npos);
}

TEST(MarketHubTest, UnsubscribeStopsDelivery) {
    ManualClock clock;
    MarketHub hub({}, clock.fn());

    EnvelopeCollectorSubscriber collector;
    const int id = hub.subscribe(&collector);
    hub.publish(underlyingQuote("AMD", 100.0, clock.now));
    EXPECT_EQ(collector.pendingCount(), 1u);

    EXPECT_TRUE(hub.unsubscribe(id));
    EXPECT_FALSE(hub.unsubscribe(id));
    hub.publish(underlyingQuote("AMD", 101.0, clock.now));
    EXPECT_EQ(collector.pendingCount(), 1u);
}
