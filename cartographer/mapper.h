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

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <clang-c/Index.h>
#include "../core/container.h"
#include "../listree/listree.h"
#include "../core/cursor.h"

namespace agentc {
namespace cartographer {

class Mapper {
public:
    struct NodeDescription {
        std::string key;
        std::string kind;
        std::string name;
        std::string type;
        std::string returnType;
        std::optional<int> size;
        std::optional<int> offset;
        CPtr<CLL<NodeDescription>> children;

        void clearChildren();
        void appendChild(const NodeDescription& child);
        size_t childCount() const;
        bool hasChildren() const;
        CPtr<NodeDescription> firstChild() const;
        void forEachChild(const std::function<void(CPtr<NodeDescription>&)>& callback) const;
    };

    struct ParseDescription {
        CPtr<CLL<NodeDescription>> symbols;

        void clearSymbols();
        void appendSymbol(const NodeDescription& symbol);
        size_t symbolCount() const;
        bool hasSymbols() const;
        CPtr<NodeDescription> firstSymbol() const;
        void forEachSymbol(const std::function<void(CPtr<NodeDescription>&)>& callback) const;
    };

    Mapper();
    ~Mapper();
    bool parseDescription(const std::string& filename, ParseDescription& out);
    CPtr<ListreeValue> parse(const std::string& filename);
    static CPtr<ListreeValue> materialize(const ParseDescription& description);
private:
    CXIndex index;
};

} // namespace cartographer
} // namespace agentc
