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

// cartographer_dump — parse a C header and write the type hierarchy as JSON
//
// Usage:  cartographer_dump <header.h>
//
// Output (stdout):  pretty-printed JSON object keyed by symbol name.
//
// Each symbol entry contains:
//   "kind"        : "Struct" | "Function" | "Field" | "Parameter" |
//                   "Typedef" | "Enum"
//   "name"        : string  (clang cursor spelling; "" for anonymous)
//   "type"        : string  (full clang type spelling)
//   "size"        : number  (sizeof in bytes; omitted for incomplete types)
//   "return_type" : string  (functions only)
//   "offset"      : number  (byte offset in parent struct; fields only)
//   "children"    : object  (always present; may be {})

#include <iostream>
#include <string>
#include <sstream>

#include "mapper.h"

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

static std::string indent(int depth) {
    return std::string(depth * 2, ' ');
}

static std::string jstr(const std::string& s) {
    return "\"" + jsonEscape(s) + "\"";
}

// ---------------------------------------------------------------------------
// Recursive serializer
// ---------------------------------------------------------------------------

static void dumpNode(std::ostream& out,
                     const Mapper::NodeDescription& node,
                     int depth)
{
    const std::string pad  = indent(depth);
    const std::string pad1 = indent(depth + 1);
    const std::string pad2 = indent(depth + 2);

    out << "{\n";

    out << pad1 << "\"kind\": "  << jstr(node.kind) << ",\n";
    out << pad1 << "\"name\": "  << jstr(node.name) << ",\n";
    out << pad1 << "\"type\": "  << jstr(node.type);

    if (node.size.has_value()) {
        out << ",\n" << pad1 << "\"size\": " << node.size.value();
    }

    if (!node.returnType.empty()) {
        out << ",\n" << pad1 << "\"return_type\": " << jstr(node.returnType);
    }

    if (node.offset.has_value()) {
        out << ",\n" << pad1 << "\"offset\": " << node.offset.value();
    }

    // children — always emitted
    out << ",\n" << pad1 << "\"children\": ";

    if (!node.hasChildren()) {
        out << "{}";
    } else {
        out << "{\n";
        bool firstChild = true;
        node.forEachChild([&](CPtr<Mapper::NodeDescription>& child) {
            if (!firstChild) out << ",\n";
            firstChild = false;
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
    if (argc < 2) {
        std::cerr << "Usage: cartographer_dump <header.h>\n";
        return 1;
    }

    const std::string path = argv[1];
    Mapper mapper;
    Mapper::ParseDescription desc;

    if (!mapper.parseDescription(path, desc)) {
        std::cerr << "cartographer_dump: failed to parse: " << path << "\n";
        return 1;
    }

    std::ostream& out = std::cout;
    out << "{\n";

    bool firstSymbol = true;
    desc.forEachSymbol([&](CPtr<Mapper::NodeDescription>& sym) {
        if (!firstSymbol) out << ",\n";
        firstSymbol = false;
        out << indent(1) << jstr(sym->key) << ": ";
        dumpNode(out, *sym, 1);
    });

    out << "\n}\n";
    return 0;
}
