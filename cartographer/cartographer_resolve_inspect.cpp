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

// cartographer_resolve_inspect — decode resolver_json_v1 and pretty-print
//
// Usage:  cartographer_resolve_inspect [resolved.json|-]
//
// Reads resolver_json_v1 (as produced by cartographer_resolve) from a file or
// stdin ("-"), decodes it via resolver::decodeResolvedApi(), and writes a
// human-readable JSON representation to stdout.
//
// Output format:
//   {
//     "library": { "path": "...", "file_size": N, ... },
//     "summary": { "symbol_count": N, "resolved_count": N, "unresolved_count": N },
//     "symbols": {
//       "SymbolKey": {
//         "kind": "...", "name": "...", "symbol": "...",
//         "resolution_status": "...", "address": "..."
//       }, ...
//     }
//   }
//
// Pipeline example:
//   cartographer_parse header.h | cartographer_resolve libfoo.so - | cartographer_resolve_inspect
//   cartographer_resolve_inspect resolved.json

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "resolver.h"

using namespace agentc;
using namespace agentc::cartographer;

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static std::string jsonEscape(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (c < 0x20) {
                    out << "\\u00";
                    out << "0123456789abcdef"[c >> 4];
                    out << "0123456789abcdef"[c & 0xf];
                } else {
                    out << c;
                }
        }
    }
    return out.str();
}

static std::string indent(int depth) { return std::string(depth * 2, ' '); }
static std::string jstr(const std::string& s) { return "\"" + jsonEscape(s) + "\""; }

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    const bool readStdin = (argc < 2 || std::string(argv[1]) == "-");

    std::string resolvedJson;
    if (readStdin) {
        resolvedJson = std::string(std::istreambuf_iterator<char>(std::cin),
                                   std::istreambuf_iterator<char>());
    } else {
        std::ifstream f(argv[1]);
        if (!f) {
            std::cerr << "cartographer_resolve_inspect: cannot open: " << argv[1] << "\n";
            return 1;
        }
        resolvedJson = std::string(std::istreambuf_iterator<char>(f),
                                   std::istreambuf_iterator<char>());
    }

    resolver::ResolvedApi api;
    std::string error;
    if (!resolver::decodeResolvedApi(resolvedJson, api, error)) {
        std::cerr << "cartographer_resolve_inspect: decode error: " << error << "\n";
        return 1;
    }

    std::ostream& out = std::cout;
    out << "{\n";

    // library block
    out << indent(1) << "\"library\": {\n";
    out << indent(2) << "\"path\": " << jstr(api.libraryPath) << ",\n";
    out << indent(2) << "\"file_size\": " << api.fileSize << ",\n";
    out << indent(2) << "\"modified_time_ns\": " << api.modifiedTimeNs << ",\n";
    out << indent(2) << "\"content_hash\": " << jstr(api.contentHash) << ",\n";
    out << indent(2) << "\"address_bindings_process_local\": "
        << (api.addressBindingsProcessLocal ? "true" : "false") << "\n";
    out << indent(1) << "},\n";

    // summary block
    out << indent(1) << "\"summary\": {\n";
    out << indent(2) << "\"symbol_count\": " << api.symbolCount << ",\n";
    out << indent(2) << "\"resolved_count\": " << api.resolvedCount << ",\n";
    out << indent(2) << "\"unresolved_count\": " << api.unresolvedCount << "\n";
    out << indent(1) << "},\n";

    // symbols block — keyed by symbol key
    out << indent(1) << "\"symbols\": {\n";
    bool firstSym = true;
    api.forEachSymbol([&](CPtr<resolver::ResolvedSymbol>& sym) {
        if (!firstSym) out << ",\n";
        firstSym = false;
        out << indent(2) << jstr(sym->key) << ": {\n";
        out << indent(3) << "\"kind\": " << jstr(sym->kind) << ",\n";
        out << indent(3) << "\"name\": " << jstr(sym->name) << ",\n";
        out << indent(3) << "\"symbol\": " << jstr(sym->symbolName) << ",\n";
        out << indent(3) << "\"resolution_status\": " << jstr(sym->resolutionStatus);
        if (!sym->address.empty()) {
            out << ",\n" << indent(3) << "\"address\": " << jstr(sym->address);
        }
        out << "\n" << indent(2) << "}";
    });
    out << "\n" << indent(1) << "}\n";

    out << "}\n";
    return 0;
}
