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

// G117 Market Data Hub — canonical event model.
//
// This module is a downstream application of AgentC facilities and is kept
// deliberately separate from the core AgentC machinery (no Edict VM, listree,
// or allocator dependencies). Events use flat numeric/text field maps so they
// serialize losslessly to JSON and convert trivially to Listree envelopes at
// the AgentC bridge boundary.

#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace agentc::markethub {

// Canonical hub event types (WP_G117 Phase 0 schema).
enum class EventType {
    UnderlyingQuote,
    OptionQuote,
    OptionChainSnapshot,
    HistorySample,
    Highlight,
};

const char* eventTypeName(EventType type);

// Parses a canonical type name; returns false when the name is unknown.
bool parseEventType(const std::string& name, EventType& out);

// One normalized market data event.
//
// Envelope fields are hub-owned; payload lives in the flat `num`/`text`
// maps. Missing numeric fields are absent — the hub never fabricates zeros
// for unavailable provider data (WP_G117 §4 data-quality rule).
struct MarketEvent {
    EventType type = EventType::UnderlyingQuote;
    std::string symbol;         // underlying symbol, uppercase
    std::string contract;       // option contract symbol; empty for underlying
    std::string source;         // "fixture" | provider | "facade:<n>" | "highlight:<rule>"
    long long sequence = 0;     // hub-stamped, strictly monotonic per bus
    long long observedUnix = 0; // provider/exchange observation time (0 = unknown)
    long long receivedUnix = 0; // hub ingest time

    std::map<std::string, double> num;
    std::map<std::string, std::string> text;

    bool hasNum(const std::string& key) const { return num.count(key) > 0; }
    double numOr(const std::string& key, double fallback) const {
        auto it = num.find(key);
        return it == num.end() ? fallback : it->second;
    }
    std::string textOr(const std::string& key, const std::string& fallback) const {
        auto it = text.find(key);
        return it == text.end() ? fallback : it->second;
    }

    // Effective freshness anchor: observed time when known, else receipt time.
    long long freshnessUnix() const {
        return observedUnix > 0 ? observedUnix : receivedUnix;
    }

    // Serializes the event as a compact JSON object.
    std::string toJson() const;
};

} // namespace agentc::markethub
