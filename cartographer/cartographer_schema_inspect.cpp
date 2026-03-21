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

// cartographer_schema_inspect — decode parser_json_v1 and pretty-print
//
// Usage:  cartographer_schema_inspect [schema.json|-]
//
// Reads parser_json_v1 (as produced by cartographer_parse) from a file or
// stdin ("-"), decodes it via protocol::decodeParseDescription(), and writes
// a human-readable JSON representation to stdout.
//
// The output format matches cartographer_dump: a dict keyed by symbol name
// with "kind", "name", "type", "size", "return_type", "offset", "children".
// Optional fields are omitted when absent (null/empty in the wire format).
//
// Pipeline example:
//   cartographer_parse header.h | cartographer_schema_inspect
//   cartographer_parse header.h > schema.json && cartographer_schema_inspect schema.json

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "mapper.h"
#include "protocol.h"

using namespace agentc;
using namespace agentc::cartographer;

// ---------------------------------------------------------------------------
// JSON helpers (same as cartographer_dump)
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

static void dumpNode(std::ostream& out, const Mapper::NodeDescription& node, int depth) {
    const std::string pad  = indent(depth);
    const std::string pad1 = indent(depth + 1);
    const std::string pad2 = indent(depth + 2);

    out << "{\n";
    out << pad1 << "\"kind\": " << jstr(node.kind) << ",\n";
    out << pad1 << "\"name\": " << jstr(node.name) << ",\n";
    out << pad1 << "\"type\": " << jstr(node.type);

    if (node.size.has_value()) {
        out << ",\n" << pad1 << "\"size\": " << node.size.value();
    }
    if (!node.returnType.empty()) {
        out << ",\n" << pad1 << "\"return_type\": " << jstr(node.returnType);
    }
    if (node.offset.has_value()) {
        out << ",\n" << pad1 << "\"offset\": " << node.offset.value();
    }

    out << ",\n" << pad1 << "\"children\": ";
    if (!node.hasChildren()) {
        out << "{}";
    } else {
        out << "{\n";
        bool first = true;
        node.forEachChild([&](CPtr<Mapper::NodeDescription>& child) {
            if (!first) out << ",\n";
            first = false;
            out << pad2 << jstr(child->key) << ": ";
            dumpNode(out, *child, depth + 2);
        });
        out << "\n" << pad1 << "}";
    }

    out << "\n" << pad << "}";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    const bool readStdin = (argc < 2 || std::string(argv[1]) == "-");

    std::string schemaJson;
    if (readStdin) {
        schemaJson = std::string(std::istreambuf_iterator<char>(std::cin),
                                  std::istreambuf_iterator<char>());
    } else {
        std::ifstream f(argv[1]);
        if (!f) {
            std::cerr << "cartographer_schema_inspect: cannot open: " << argv[1] << "\n";
            return 1;
        }
        schemaJson = std::string(std::istreambuf_iterator<char>(f),
                                  std::istreambuf_iterator<char>());
    }

    Mapper::ParseDescription desc;
    std::string error;
    if (!protocol::decodeParseDescription(schemaJson, desc, error)) {
        std::cerr << "cartographer_schema_inspect: decode error: " << error << "\n";
        return 1;
    }

    std::ostream& out = std::cout;
    out << "{\n";
    bool first = true;
    desc.forEachSymbol([&](CPtr<Mapper::NodeDescription>& sym) {
        if (!first) out << ",\n";
        first = false;
        out << indent(1) << jstr(sym->key) << ": ";
        dumpNode(out, *sym, 1);
    });
    out << "\n}\n";
    return 0;
}
