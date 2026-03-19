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

#include "protocol.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>

namespace agentc {
namespace cartographer {
namespace protocol {

namespace {

static const char* importVerb() {
    return "import_request";
}

static const char* statusVerb() {
    return "import_status";
}

static const char* executionModeName(ImportExecutionMode mode) {
    switch (mode) {
        case ImportExecutionMode::Sync:
            return "sync";
        case ImportExecutionMode::Deferred:
            return "deferred";
    }
    return "sync";
}

static bool parseExecutionMode(const std::string& text, ImportExecutionMode& out) {
    if (text == "sync") {
        out = ImportExecutionMode::Sync;
        return true;
    }
    if (text == "deferred") {
        out = ImportExecutionMode::Deferred;
        return true;
    }
    return false;
}

static std::string quote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

static std::string escapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(ch);
                break;
        }
    }
    return out;
}

static void appendJsonNode(const Mapper::NodeDescription& node,
                                   std::string& out) {
    out += "{\"key\":\"" + escapeJsonString(node.key) +
           "\",\"kind\":\"" + escapeJsonString(node.kind) +
           "\",\"name\":\"" + escapeJsonString(node.name) +
           "\",\"type\":\"" + escapeJsonString(node.type) +
           "\",\"return_type\":\"" + escapeJsonString(node.returnType) +
           "\",\"size\":";
    out += node.size ? std::to_string(*node.size) : "null";
    out += ",\"offset\":";
    out += node.offset ? std::to_string(*node.offset) : "null";
    out += ",\"children\":[";
    bool firstChild = true;
    node.forEachChild([&](CPtr<Mapper::NodeDescription>& child) {
        if (!child) {
            return;
        }
        if (!firstChild) {
            out.push_back(',');
        }
        firstChild = false;
        appendJsonNode(*child, out);
    });
    out += "]}";
}

struct JsonParser {
    const std::string& input;
    size_t position = 0;

    void skipWhitespace() {
        while (position < input.size() && std::isspace(static_cast<unsigned char>(input[position]))) {
            ++position;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (position >= input.size() || input[position] != expected) {
            return false;
        }
        ++position;
        return true;
    }

    bool parseString(std::string& out, std::string& error) {
        skipWhitespace();
        if (position >= input.size() || input[position] != '"') {
            error = "Expected JSON string";
            return false;
        }
        ++position;
        out.clear();
        while (position < input.size()) {
            char ch = input[position++];
            if (ch == '"') {
                return true;
            }
            if (ch != '\\') {
                out.push_back(ch);
                continue;
            }
            if (position >= input.size()) {
                error = "JSON string ends with dangling escape";
                return false;
            }
            char escaped = input[position++];
            switch (escaped) {
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default:
                    error = std::string("Unsupported JSON escape: ") + escaped;
                    return false;
            }
        }
        error = "JSON string is missing a closing quote";
        return false;
    }

    bool parseInt(std::optional<int>& out, std::string& error) {
        skipWhitespace();
        if (position >= input.size()) {
            error = "Expected JSON integer or null";
            return false;
        }
        if (input.compare(position, 4, "null") == 0) {
            position += 4;
            out.reset();
            return true;
        }
        size_t start = position;
        if (input[position] == '-') {
            ++position;
        }
        if (position >= input.size() || !std::isdigit(static_cast<unsigned char>(input[position]))) {
            error = "Expected JSON integer";
            return false;
        }
        while (position < input.size() && std::isdigit(static_cast<unsigned char>(input[position]))) {
            ++position;
        }
        char* end = nullptr;
        long parsed = std::strtol(input.substr(start, position - start).c_str(), &end, 10);
        if (!end || *end != '\0') {
            error = "Invalid JSON integer";
            return false;
        }
        out = static_cast<int>(parsed);
        return true;
    }
};

static bool parseNode(JsonParser& parser, Mapper::NodeDescription& out, std::string& error) {
    if (!parser.consume('{')) {
        error = "Expected JSON object for API node";
        return false;
    }

    bool first = true;
    while (true) {
        parser.skipWhitespace();
        if (parser.consume('}')) {
            return true;
        }
        if (!first && !parser.consume(',')) {
            error = "Expected ',' between JSON object fields";
            return false;
        }
        first = false;

        std::string key;
        if (!parser.parseString(key, error)) {
            return false;
        }
        if (!parser.consume(':')) {
            error = "Expected ':' after JSON object key";
            return false;
        }

        if (key == "key") {
            if (!parser.parseString(out.key, error)) return false;
        } else if (key == "kind") {
            if (!parser.parseString(out.kind, error)) return false;
        } else if (key == "name") {
            if (!parser.parseString(out.name, error)) return false;
        } else if (key == "type") {
            if (!parser.parseString(out.type, error)) return false;
        } else if (key == "return_type") {
            if (!parser.parseString(out.returnType, error)) return false;
        } else if (key == "size") {
            if (!parser.parseInt(out.size, error)) return false;
        } else if (key == "offset") {
            if (!parser.parseInt(out.offset, error)) return false;
        } else if (key == "children") {
            if (!parser.consume('[')) {
                error = "Expected JSON array for children";
                return false;
            }
            out.clearChildren();
            bool firstChild = true;
            while (true) {
                parser.skipWhitespace();
                if (parser.consume(']')) {
                    break;
                }
                if (!firstChild && !parser.consume(',')) {
                    error = "Expected ',' between child nodes";
                    return false;
                }
                firstChild = false;
                Mapper::NodeDescription child;
                if (!parseNode(parser, child, error)) {
                    return false;
                }
                out.appendChild(child);
            }
        } else {
            error = "Unexpected JSON field in API schema: " + key;
            return false;
        }
    }
}

static bool parseSchemaRoot(JsonParser& parser, Mapper::ParseDescription& out, std::string& error) {
    if (!parser.consume('{')) {
        error = "Expected JSON object for API schema";
        return false;
    }

    std::string key;
    if (!parser.parseString(key, error)) {
        return false;
    }
    if (key != "symbols") {
        error = "Expected top-level 'symbols' field in API schema";
        return false;
    }
    if (!parser.consume(':') || !parser.consume('[')) {
        error = "Expected JSON array for top-level symbols";
        return false;
    }

    out.clearSymbols();
    bool first = true;
    while (true) {
        parser.skipWhitespace();
        if (parser.consume(']')) {
            break;
        }
        if (!first && !parser.consume(',')) {
            error = "Expected ',' between top-level symbols";
            return false;
        }
        first = false;
        Mapper::NodeDescription symbol;
        if (!parseNode(parser, symbol, error)) {
            return false;
        }
        out.appendSymbol(symbol);
    }

    if (!parser.consume('}')) {
        error = "Expected closing JSON object for API schema";
        return false;
    }
    parser.skipWhitespace();
    if (parser.position != parser.input.size()) {
        error = "Unexpected trailing data after API schema JSON";
        return false;
    }
    return true;
}

struct TokenReader {
    const std::string& message;
    size_t position = 0;

    void skipWhitespace() {
        while (position < message.size() && std::isspace(static_cast<unsigned char>(message[position]))) {
            ++position;
        }
    }

    bool readToken(std::string& token, std::string& error) {
        skipWhitespace();
        if (position >= message.size()) {
            error = "Protocol message ended unexpectedly";
            return false;
        }
        if (message[position] == '"') {
            ++position;
            token.clear();
            while (position < message.size()) {
                char ch = message[position++];
                if (ch == '\\') {
                    if (position >= message.size()) {
                        error = "Protocol string ends with dangling escape";
                        return false;
                    }
                    token.push_back(message[position++]);
                    continue;
                }
                if (ch == '"') {
                    return true;
                }
                token.push_back(ch);
            }
            error = "Protocol string is missing a closing quote";
            return false;
        }

        size_t start = position;
        while (position < message.size() && !std::isspace(static_cast<unsigned char>(message[position]))) {
            ++position;
        }
        token.assign(message, start, position - start);
        return true;
    }
};

static bool expectToken(TokenReader& reader,
                        const char* expected,
                        std::string& error) {
    std::string token;
    if (!reader.readToken(token, error)) {
        return false;
    }
    if (token != expected) {
        error = std::string("Expected protocol token '") + expected + "'";
        return false;
    }
    return true;
}

static bool decodeImportRequestFields(TokenReader& reader,
                                      ImportRequest& out,
                                      std::string& error) {
    if (!expectToken(reader, "library", error) ||
        !reader.readToken(out.libraryPath, error) ||
        !expectToken(reader, "header", error) ||
        !reader.readToken(out.headerPath, error) ||
        !expectToken(reader, "scope", error) ||
        !reader.readToken(out.scopeName, error) ||
        !expectToken(reader, "mode", error)) {
        return false;
    }

    std::string modeText;
    if (!reader.readToken(modeText, error)) {
        return false;
    }
    if (!parseExecutionMode(modeText, out.executionMode)) {
        error = "Unsupported protocol execution mode: " + modeText;
        return false;
    }
    if (!expectToken(reader, "request_id", error) ||
        !reader.readToken(out.requestId, error)) {
        return false;
    }
    return true;
}

static bool expectEndVerb(TokenReader& reader,
                          const char* expectedVerb,
                          std::string& error) {
    std::string verb;
    if (!reader.readToken(verb, error)) {
        return false;
    }
    if (verb != expectedVerb) {
        error = std::string("Expected protocol verb '") + expectedVerb + "'";
        return false;
    }
    reader.skipWhitespace();
    if (reader.position != reader.message.size()) {
        error = "Unexpected trailing data after protocol verb";
        return false;
    }
    return true;
}

} // namespace

const char* versionName() {
    return "protocol_v1";
}

std::string encodeImportRequest(const ImportRequest& request) {
    return std::string(versionName()) +
           " library " + quote(request.libraryPath) +
           " header " + quote(request.headerPath) +
           " scope " + quote(request.scopeName) +
           " mode " + quote(executionModeName(request.executionMode)) +
           " request_id " + quote(request.requestId) +
           " " + importVerb();
}

bool decodeImportRequest(const std::string& message, ImportRequest& out, std::string& error) {
    TokenReader reader{message};
    std::string version;
    if (!reader.readToken(version, error)) {
        return false;
    }
    if (version != versionName()) {
        error = "Unsupported protocol version: " + version;
        return false;
    }
    out = {};
    if (!decodeImportRequestFields(reader, out, error)) {
        return false;
    }
    return expectEndVerb(reader, importVerb(), error);
}

std::string encodeImportStatus(const ImportRequest& request,
                               const ImportResult& result,
                               const Mapper::ParseDescription* description) {
    return std::string(versionName()) +
           " library " + quote(request.libraryPath) +
           " header " + quote(request.headerPath) +
           " scope " + quote(request.scopeName) +
           " mode " + quote(executionModeName(result.executionMode)) +
           " request_id " + quote(result.requestId) +
           " status " + quote(result.status) +
           " symbol_count " + quote(std::to_string(result.symbolCount)) +
           " error " + quote(result.error) +
           " api_schema_format " + quote(parserSchemaFormatName()) +
           " api_schema_json " + quote(description ? encodeParseDescription(*description) : std::string()) +
           " " + statusVerb();
}

std::string encodeParseDescription(const Mapper::ParseDescription& description) {
    std::string encoded = "{\"symbols\":[";
    bool firstSymbol = true;
    description.forEachSymbol([&](CPtr<Mapper::NodeDescription>& symbol) {
        if (!symbol) {
            return;
        }
        if (!firstSymbol) {
            encoded.push_back(',');
        }
        firstSymbol = false;
        appendJsonNode(*symbol, encoded);
    });
    encoded += "]}";
    return encoded;
}

bool decodeParseDescription(const std::string& encoded,
                            Mapper::ParseDescription& out,
                            std::string& error) {
    out = {};
    if (encoded.empty()) {
        return true;
    }

    JsonParser parser{encoded};
    return parseSchemaRoot(parser, out, error);
}

const char* parserSchemaFormatName() {
    return "parser_json_v1";
}

bool decodeImportStatus(const std::string& message,
                        ImportRequest& requestOut,
                        ImportResult& resultOut,
                        Mapper::ParseDescription* descriptionOut,
                        std::string& error) {
    TokenReader reader{message};
    std::string version;
    if (!reader.readToken(version, error)) {
        return false;
    }
    if (version != versionName()) {
        error = "Unsupported protocol version: " + version;
        return false;
    }
    requestOut = {};
    if (!decodeImportRequestFields(reader, requestOut, error)) {
        return false;
    }

    resultOut = {};
    resultOut.executionMode = requestOut.executionMode;
    resultOut.requestId = requestOut.requestId;
    resultOut.scopeName = requestOut.scopeName;
    if (!expectToken(reader, "status", error) ||
        !reader.readToken(resultOut.status, error) ||
        !expectToken(reader, "symbol_count", error)) {
        return false;
    }

    std::string symbolCount;
    if (!reader.readToken(symbolCount, error)) {
        return false;
    }
    char* end = nullptr;
    unsigned long long parsed = std::strtoull(symbolCount.c_str(), &end, 10);
    if (!end || *end != '\0') {
        error = "Protocol symbol_count is not numeric: " + symbolCount;
        return false;
    }
    resultOut.symbolCount = static_cast<size_t>(parsed);
    if (!expectToken(reader, "error", error) ||
        !reader.readToken(resultOut.error, error) ||
        !expectToken(reader, "api_schema_format", error)) {
        return false;
    }

    std::string schemaFormat;
    if (!reader.readToken(schemaFormat, error)) {
        return false;
    }
    if (schemaFormat != parserSchemaFormatName()) {
        error = "Unsupported API schema format: " + schemaFormat;
        return false;
    }
    if (!expectToken(reader, "api_schema_json", error)) {
        return false;
    }

    std::string encodedDescription;
    if (!reader.readToken(encodedDescription, error)) {
        return false;
    }
    resultOut.ok = (resultOut.status == "queued" ||
                    resultOut.status == "running" ||
                    resultOut.status == "ready");
    if (descriptionOut && !decodeParseDescription(encodedDescription, *descriptionOut, error)) {
        return false;
    }
    return expectEndVerb(reader, statusVerb(), error);
}

} // namespace protocol
} // namespace cartographer
} // namespace agentc
