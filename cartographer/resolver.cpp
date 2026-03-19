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

#include "resolver.h"

#include "protocol.h"

#include <dlfcn.h>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <sys/stat.h>

namespace agentc {
namespace cartographer {
namespace resolver {

namespace {

static CPtr<CLL<ResolvedSymbol>> ensureResolvedSymbolList(CPtr<CLL<ResolvedSymbol>>& list) {
    if (!list) {
        SlabId sid = Allocator<CLL<ResolvedSymbol>>::getAllocator().allocate();
        list = CPtr<CLL<ResolvedSymbol>>(sid);
    }
    return list;
}

static std::string escapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

static std::string toHexAddress(const void* ptr) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << reinterpret_cast<uintptr_t>(ptr);
    return stream.str();
}

static std::string computeFileHash(const std::string& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Failed to open library for hashing: " + path;
        return {};
    }

    uint64_t hash = 1469598103934665603ULL;
    char buffer[4096];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize count = input.gcount();
        for (std::streamsize i = 0; i < count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= 1099511628211ULL;
        }
    }
    if (!input.eof()) {
        error = "Failed while hashing library: " + path;
        return {};
    }

    std::ostringstream stream;
    stream << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

static bool readLibraryMetadata(const std::string& path,
                                uint64_t& fileSize,
                                uint64_t& modifiedTimeNs,
                                std::string& contentHash,
                                std::string& error) {
    struct stat info;
    if (::stat(path.c_str(), &info) != 0) {
        error = "Failed to stat library: " + path;
        return false;
    }

    fileSize = static_cast<uint64_t>(info.st_size);
#if defined(__APPLE__)
    modifiedTimeNs = static_cast<uint64_t>(info.st_mtimespec.tv_sec) * 1000000000ULL +
                     static_cast<uint64_t>(info.st_mtimespec.tv_nsec);
#else
    modifiedTimeNs = static_cast<uint64_t>(info.st_mtim.tv_sec) * 1000000000ULL +
                     static_cast<uint64_t>(info.st_mtim.tv_nsec);
#endif
    contentHash = computeFileHash(path, error);
    return !contentHash.empty();
}

static std::string symbolNameForNode(const Mapper::NodeDescription& node) {
    if (!node.name.empty()) {
        return node.name;
    }
    return node.key;
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

    bool parseUnsigned(uint64_t& out, std::string& error) {
        skipWhitespace();
        if (position >= input.size() || !std::isdigit(static_cast<unsigned char>(input[position]))) {
            error = "Expected JSON unsigned integer";
            return false;
        }
        size_t start = position;
        while (position < input.size() && std::isdigit(static_cast<unsigned char>(input[position]))) {
            ++position;
        }
        char* end = nullptr;
        unsigned long long parsed = std::strtoull(input.substr(start, position - start).c_str(), &end, 10);
        if (!end || *end != '\0') {
            error = "Invalid JSON unsigned integer";
            return false;
        }
        out = static_cast<uint64_t>(parsed);
        return true;
    }

    bool parseBool(bool& out, std::string& error) {
        skipWhitespace();
        if (input.compare(position, 4, "true") == 0) {
            position += 4;
            out = true;
            return true;
        }
        if (input.compare(position, 5, "false") == 0) {
            position += 5;
            out = false;
            return true;
        }
        error = "Expected JSON boolean";
        return false;
    }
};

static bool parseResolvedSymbol(JsonParser& parser, ResolvedSymbol& out, std::string& error) {
    if (!parser.consume('{')) {
        error = "Expected JSON object for resolved symbol";
        return false;
    }

    bool first = true;
    while (true) {
        parser.skipWhitespace();
        if (parser.consume('}')) {
            return true;
        }
        if (!first && !parser.consume(',')) {
            error = "Expected ',' between resolved symbol fields";
            return false;
        }
        first = false;

        std::string key;
        if (!parser.parseString(key, error)) {
            return false;
        }
        if (!parser.consume(':')) {
            error = "Expected ':' after resolved symbol key";
            return false;
        }

        if (key == "key") {
            if (!parser.parseString(out.key, error)) return false;
        } else if (key == "kind") {
            if (!parser.parseString(out.kind, error)) return false;
        } else if (key == "name") {
            if (!parser.parseString(out.name, error)) return false;
        } else if (key == "symbol") {
            if (!parser.parseString(out.symbolName, error)) return false;
        } else if (key == "resolution_status") {
            if (!parser.parseString(out.resolutionStatus, error)) return false;
        } else if (key == "address") {
            if (!parser.parseString(out.address, error)) return false;
        } else {
            error = "Unexpected JSON field in resolved symbol: " + key;
            return false;
        }
    }
}

static bool parseResolvedApi(JsonParser& parser, ResolvedApi& out, std::string& error) {
    if (!parser.consume('{')) {
        error = "Expected JSON object for resolved API";
        return false;
    }

    out = ResolvedApi();
    bool first = true;
    while (true) {
        parser.skipWhitespace();
        if (parser.consume('}')) {
            parser.skipWhitespace();
            if (parser.position != parser.input.size()) {
                error = "Unexpected trailing data after resolved API JSON";
                return false;
            }
            return true;
        }
        if (!first && !parser.consume(',')) {
            error = "Expected ',' between resolved API fields";
            return false;
        }
        first = false;

        std::string key;
        if (!parser.parseString(key, error)) {
            return false;
        }
        if (!parser.consume(':')) {
            error = "Expected ':' after resolved API key";
            return false;
        }

        if (key == "format") {
            std::string format;
            if (!parser.parseString(format, error)) return false;
            if (format != resolverSchemaFormatName()) {
                error = "Unexpected resolved API format: " + format;
                return false;
            }
        } else if (key == "library") {
            if (!parser.consume('{')) {
                error = "Expected JSON object for library metadata";
                return false;
            }
            bool firstLibrary = true;
            while (true) {
                parser.skipWhitespace();
                if (parser.consume('}')) {
                    break;
                }
                if (!firstLibrary && !parser.consume(',')) {
                    error = "Expected ',' between library metadata fields";
                    return false;
                }
                firstLibrary = false;

                std::string libraryKey;
                if (!parser.parseString(libraryKey, error)) return false;
                if (!parser.consume(':')) {
                    error = "Expected ':' after library metadata key";
                    return false;
                }

                if (libraryKey == "path") {
                    if (!parser.parseString(out.libraryPath, error)) return false;
                } else if (libraryKey == "file_size") {
                    if (!parser.parseUnsigned(out.fileSize, error)) return false;
                } else if (libraryKey == "modified_time_ns") {
                    if (!parser.parseUnsigned(out.modifiedTimeNs, error)) return false;
                } else if (libraryKey == "content_hash") {
                    if (!parser.parseString(out.contentHash, error)) return false;
                } else if (libraryKey == "address_bindings_process_local") {
                    if (!parser.parseBool(out.addressBindingsProcessLocal, error)) return false;
                } else {
                    error = "Unexpected library metadata field: " + libraryKey;
                    return false;
                }
            }
        } else if (key == "summary") {
            if (!parser.consume('{')) {
                error = "Expected JSON object for summary metadata";
                return false;
            }
            bool firstSummary = true;
            while (true) {
                parser.skipWhitespace();
                if (parser.consume('}')) {
                    break;
                }
                if (!firstSummary && !parser.consume(',')) {
                    error = "Expected ',' between summary fields";
                    return false;
                }
                firstSummary = false;

                std::string summaryKey;
                if (!parser.parseString(summaryKey, error)) return false;
                if (!parser.consume(':')) {
                    error = "Expected ':' after summary key";
                    return false;
                }

                if (summaryKey == "symbol_count") {
                    uint64_t value = 0;
                    if (!parser.parseUnsigned(value, error)) return false;
                    out.symbolCount = static_cast<size_t>(value);
                } else if (summaryKey == "resolved_count") {
                    uint64_t value = 0;
                    if (!parser.parseUnsigned(value, error)) return false;
                    out.resolvedCount = static_cast<size_t>(value);
                } else if (summaryKey == "unresolved_count") {
                    uint64_t value = 0;
                    if (!parser.parseUnsigned(value, error)) return false;
                    out.unresolvedCount = static_cast<size_t>(value);
                } else {
                    error = "Unexpected summary field: " + summaryKey;
                    return false;
                }
            }
        } else if (key == "api_schema_format") {
            if (!parser.parseString(out.parserSchemaFormat, error)) return false;
        } else if (key == "api_schema_json") {
            if (!parser.parseString(out.parserSchemaJson, error)) return false;
        } else if (key == "symbols") {
            if (!parser.consume('[')) {
                error = "Expected JSON array for resolved symbols";
                return false;
            }
            out.clearSymbols();
            bool firstSymbol = true;
            while (true) {
                parser.skipWhitespace();
                if (parser.consume(']')) {
                    break;
                }
                if (!firstSymbol && !parser.consume(',')) {
                    error = "Expected ',' between resolved symbols";
                    return false;
                }
                firstSymbol = false;
                ResolvedSymbol symbol;
                if (!parseResolvedSymbol(parser, symbol, error)) {
                    return false;
                }
                out.appendSymbol(symbol);
            }
        } else {
            error = "Unexpected top-level resolved API field: " + key;
            return false;
        }
    }
}

} // namespace

const char* resolverSchemaFormatName() {
    return "resolver_json_v1";
}

void ResolvedApi::clearSymbols() {
    symbols = nullptr;
}

void ResolvedApi::appendSymbol(const ResolvedSymbol& symbol) {
    CPtr<CLL<ResolvedSymbol>> list = ensureResolvedSymbolList(symbols);
    CPtr<CLL<ResolvedSymbol>> node(symbol);
    list->store(node, false);
}

bool ResolvedApi::hasSymbols() const {
    return symbols && symbols->get(true).data;
}

CPtr<ResolvedSymbol> ResolvedApi::firstSymbol() const {
    if (!symbols) {
        return nullptr;
    }
    return symbols->get(true).data;
}

void ResolvedApi::forEachSymbol(const std::function<void(CPtr<ResolvedSymbol>&)>& callback) const {
    if (symbols) {
        symbols->forEach(callback, true);
    }
}

bool resolveApiDescription(const std::string& libraryPath,
                           const Mapper::ParseDescription& description,
                           ResolvedApi& out,
                           std::string& error,
                           bool includeAddresses) {
    if (libraryPath.empty()) {
        error = "Cartographer resolver requires a library path";
        return false;
    }

    uint64_t fileSize = 0;
    uint64_t modifiedTimeNs = 0;
    std::string contentHash;
    if (!readLibraryMetadata(libraryPath, fileSize, modifiedTimeNs, contentHash, error)) {
        return false;
    }

    void* handle = ::dlopen(libraryPath.c_str(), RTLD_LAZY);
    if (!handle) {
        error = "Failed to load library: " + libraryPath;
        return false;
    }

    out = ResolvedApi();
    out.libraryPath = libraryPath;
    out.fileSize = fileSize;
    out.modifiedTimeNs = modifiedTimeNs;
    out.contentHash = contentHash;
    out.addressBindingsProcessLocal = includeAddresses;
    out.parserSchemaFormat = protocol::parserSchemaFormatName();
    out.parserSchemaJson = protocol::encodeParseDescription(description);
    out.symbolCount = description.symbolCount();

    description.forEachSymbol([&](CPtr<Mapper::NodeDescription>& nodePtr) {
        if (!nodePtr) {
            return;
        }
        const Mapper::NodeDescription& node = *nodePtr;
        ResolvedSymbol symbol;
        symbol.key = node.key;
        symbol.kind = node.kind;
        symbol.name = node.name;
        symbol.symbolName = symbolNameForNode(node);

        if (node.kind != "Function") {
            symbol.resolutionStatus = "not_applicable";
        } else {
            void* address = ::dlsym(handle, symbol.symbolName.c_str());
            if (address) {
                symbol.resolutionStatus = "resolved";
                ++out.resolvedCount;
                if (includeAddresses) {
                    symbol.address = toHexAddress(address);
                }
            } else {
                symbol.resolutionStatus = "unresolved";
                ++out.unresolvedCount;
            }
        }

        out.appendSymbol(symbol);
    });

    ::dlclose(handle);
    return true;
}

std::string encodeResolvedApi(const ResolvedApi& api) {
    std::string out;
    out += "{\"format\":\"";
    out += resolverSchemaFormatName();
    out += "\",\"library\":{\"path\":\"" + escapeJsonString(api.libraryPath) +
           "\",\"file_size\":" + std::to_string(api.fileSize) +
           ",\"modified_time_ns\":" + std::to_string(api.modifiedTimeNs) +
           ",\"content_hash\":\"" + escapeJsonString(api.contentHash) +
           "\",\"address_bindings_process_local\":" +
           std::string(api.addressBindingsProcessLocal ? "true" : "false") +
           "},\"summary\":{\"symbol_count\":" + std::to_string(api.symbolCount) +
           ",\"resolved_count\":" + std::to_string(api.resolvedCount) +
           ",\"unresolved_count\":" + std::to_string(api.unresolvedCount) +
           "},\"api_schema_format\":\"" + escapeJsonString(api.parserSchemaFormat) +
           "\",\"api_schema_json\":\"" + escapeJsonString(api.parserSchemaJson) +
           "\",\"symbols\":[";
    bool firstSymbol = true;
    api.forEachSymbol([&](CPtr<ResolvedSymbol>& symbolPtr) {
        if (!firstSymbol) {
            out.push_back(',');
        }
        firstSymbol = false;
        const ResolvedSymbol& symbol = *symbolPtr;
        out += "{\"key\":\"" + escapeJsonString(symbol.key) +
               "\",\"kind\":\"" + escapeJsonString(symbol.kind) +
               "\",\"name\":\"" + escapeJsonString(symbol.name) +
               "\",\"symbol\":\"" + escapeJsonString(symbol.symbolName) +
               "\",\"resolution_status\":\"" + escapeJsonString(symbol.resolutionStatus) + "\"";
        if (!symbol.address.empty()) {
            out += ",\"address\":\"" + escapeJsonString(symbol.address) + "\"";
        }
        out += "}";
    });
    out += "]}";
    return out;
}

bool decodeResolvedApi(const std::string& json, ResolvedApi& out, std::string& error) {
    JsonParser parser{json};
    if (!parseResolvedApi(parser, out, error)) {
        return false;
    }
    if (out.parserSchemaFormat != protocol::parserSchemaFormatName()) {
        error = "Unexpected resolved API schema format: " + out.parserSchemaFormat;
        return false;
    }
    if (out.parserSchemaJson.empty()) {
        error = "Resolved API JSON is missing api_schema_json";
        return false;
    }
    return true;
}

} // namespace resolver
} // namespace cartographer
} // namespace agentc
