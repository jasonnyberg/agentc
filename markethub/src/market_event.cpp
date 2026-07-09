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

#include "markethub/market_event.h"

#include <sstream>

namespace agentc::markethub {

namespace {

std::string escapeJson(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
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

} // namespace

const char* eventTypeName(EventType type) {
    switch (type) {
        case EventType::UnderlyingQuote:     return "underlying_quote";
        case EventType::OptionQuote:         return "option_quote";
        case EventType::OptionChainSnapshot: return "option_chain_snapshot";
        case EventType::HistorySample:       return "history_sample";
        case EventType::Highlight:           return "highlight";
    }
    return "unknown";
}

bool parseEventType(const std::string& name, EventType& out) {
    if (name == "underlying_quote")      { out = EventType::UnderlyingQuote; return true; }
    if (name == "option_quote")          { out = EventType::OptionQuote; return true; }
    if (name == "option_chain_snapshot") { out = EventType::OptionChainSnapshot; return true; }
    if (name == "history_sample")        { out = EventType::HistorySample; return true; }
    if (name == "highlight")             { out = EventType::Highlight; return true; }
    return false;
}

std::string MarketEvent::toJson() const {
    std::ostringstream out;
    out << "{\"type\":\"" << eventTypeName(type) << "\""
        << ",\"symbol\":\"" << escapeJson(symbol) << "\""
        << ",\"contract\":\"" << escapeJson(contract) << "\""
        << ",\"source\":\"" << escapeJson(source) << "\""
        << ",\"sequence\":" << sequence
        << ",\"observed_unix\":" << observedUnix
        << ",\"received_unix\":" << receivedUnix;

    out << ",\"num\":{";
    bool first = true;
    for (const auto& [key, value] : num) {
        if (!first) out << ",";
        first = false;
        out << "\"" << escapeJson(key) << "\":" << value;
    }
    out << "}";

    out << ",\"text\":{";
    first = true;
    for (const auto& [key, value] : text) {
        if (!first) out << ",";
        first = false;
        out << "\"" << escapeJson(key) << "\":\"" << escapeJson(value) << "\"";
    }
    out << "}}";
    return out.str();
}

} // namespace agentc::markethub
