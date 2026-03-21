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

#include "mapper.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <atomic>

namespace agentc {
namespace cartographer {

namespace {

using NodeDescription = Mapper::NodeDescription;
using ParseDescription = Mapper::ParseDescription;

static CPtr<CLL<NodeDescription>> ensureNodeDescriptionList(CPtr<CLL<NodeDescription>>& list) {
    if (!list) {
        SlabId sid = Allocator<CLL<NodeDescription>>::getAllocator().allocate();
        list = CPtr<CLL<NodeDescription>>(sid);
    }
    return list;
}

static std::string cursorSpelling(CXString value) {
    const char* text = clang_getCString(value);
    std::string out = text ? text : "";
    clang_disposeString(value);
    return out;
}

static std::string cursorKindName(CXCursorKind kind) {
    if (kind == CXCursor_StructDecl) return "Struct";
    if (kind == CXCursor_FunctionDecl) return "Function";
    if (kind == CXCursor_FieldDecl) return "Field";
    if (kind == CXCursor_TypedefDecl) return "Typedef";
    if (kind == CXCursor_EnumDecl) return "Enum";
    if (kind == CXCursor_ParmDecl) return "Parameter";
    return {};
}

static NodeDescription describeCursor(CXCursor cursor, CXCursor parent) {
    static std::atomic<int> anonCount{0};

    NodeDescription node;
    node.name = cursorSpelling(clang_getCursorSpelling(cursor));
    node.kind = cursorKindName(clang_getCursorKind(cursor));
    node.key = node.name.empty() ? "anonymous_" + std::to_string(anonCount.fetch_add(1, std::memory_order_relaxed)) : node.name;

    CXType type = clang_getCursorType(cursor);
    node.type = cursorSpelling(clang_getTypeSpelling(type));

    long long size = clang_Type_getSizeOf(type);
    if (size >= 0) {
        node.size = static_cast<int>(size);
    }

    if (clang_getCursorKind(cursor) == CXCursor_FunctionDecl) {
        node.returnType = cursorSpelling(clang_getTypeSpelling(clang_getCursorResultType(cursor)));
    } else if (clang_getCursorKind(cursor) == CXCursor_FieldDecl) {
        long long offset = clang_Type_getOffsetOf(clang_getCursorType(parent), node.name.c_str());
        if (offset >= 0) {
            node.offset = static_cast<int>(offset / 8);
        }
    }

    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor childParent, CXClientData clientData) {
            auto* node = static_cast<NodeDescription*>(clientData);
            const std::string kind = cursorKindName(clang_getCursorKind(child));
            if (kind.empty()) {
                return CXChildVisit_Recurse;
            }
            node->appendChild(describeCursor(child, childParent));
            return CXChildVisit_Continue;
        },
        &node);

    return node;
}

// bindTypes: post-materialize pass that resolves "struct X" field type strings
// into direct type_def references.  For every Struct in root, each Field child
// whose type string starts with "struct " is looked up in the root namespace;
// if found, addNamedItem stores the resolved CPtr<ListreeValue> as "type_def"
// on the field node — a shared (ref-counted) reference, not a copy.
static void bindTypes(CPtr<ListreeValue> root) {
    if (!root) return;
    static const std::string kStructPrefix = "struct ";
    root->forEachTree([&](const std::string& /*symbolName*/, CPtr<ListreeItem>& symbolItem) {
        auto symbolVal = symbolItem ? symbolItem->getValue(false, false) : nullptr;
        if (!symbolVal) return;
        auto kindItem = symbolVal->find("kind");
        auto kindVal  = kindItem ? kindItem->getValue(false, false) : nullptr;
        if (!kindVal || !kindVal->getData() || kindVal->getLength() == 0) return;
        std::string kind(static_cast<const char*>(kindVal->getData()), kindVal->getLength());
        if (kind != "Struct") return;

        auto childrenItem = symbolVal->find("children");
        auto childrenVal  = childrenItem ? childrenItem->getValue(false, false) : nullptr;
        if (!childrenVal) return;

        childrenVal->forEachTree([&](const std::string& /*fieldName*/, CPtr<ListreeItem>& fieldItem) {
            auto fieldVal = fieldItem ? fieldItem->getValue(false, false) : nullptr;
            if (!fieldVal) return;
            auto typeItem = fieldVal->find("type");
            auto typeVal  = typeItem ? typeItem->getValue(false, false) : nullptr;
            if (!typeVal || !typeVal->getData() || typeVal->getLength() == 0) return;
            if ((typeVal->getFlags() & LtvFlags::Binary) != LtvFlags::None) return;
            std::string fieldType(static_cast<const char*>(typeVal->getData()), typeVal->getLength());
            if (fieldType.size() <= kStructPrefix.size() ||
                fieldType.substr(0, kStructPrefix.size()) != kStructPrefix) return;
            std::string lookupName = fieldType.substr(kStructPrefix.size());
            auto nsItem       = root->find(lookupName);
            auto nestedTypeDef = nsItem ? nsItem->getValue(false, false) : nullptr;
            if (!nestedTypeDef) return;
            addNamedItem(fieldVal, "type_def", nestedTypeDef);
        });
    });
}

static CPtr<ListreeValue> materializeNode(const NodeDescription& description) {
    CPtr<ListreeValue> nodeVal = createNullValue();
    addNamedItem(nodeVal, "kind", createStringValue(description.kind));
    addNamedItem(nodeVal, "name", createStringValue(description.name));
    addNamedItem(nodeVal, "type", createStringValue(description.type));
    if (description.size.has_value()) {
        int size = *description.size;
        addNamedItem(nodeVal, "size", createBinaryValue(&size, sizeof(int)));
    }
    if (!description.returnType.empty()) {
        addNamedItem(nodeVal, "return_type", createStringValue(description.returnType));
    }
    if (description.offset.has_value()) {
        int offset = *description.offset;
        addNamedItem(nodeVal, "offset", createBinaryValue(&offset, sizeof(int)));
    }

    CPtr<ListreeValue> childrenVal = createNullValue();
    description.forEachChild([&](CPtr<NodeDescription>& child) {
        if (child) {
            addNamedItem(childrenVal, child->key, materializeNode(*child));
        }
    });
    addNamedItem(nodeVal, "children", childrenVal);
    return nodeVal;
}

static bool parseDescriptionFallback(const std::string& filename, ParseDescription& out) {
    std::ifstream ifs(filename);
    if (!ifs.is_open()) {
        std::error_code ec;
        auto canon = std::filesystem::weakly_canonical(std::filesystem::path(filename), ec);
        if (!ec) {
            ifs.open(canon);
        }
    }
    if (!ifs.is_open()) {
        return false;
    }

    std::ostringstream oss;
    oss << ifs.rdbuf();
    std::string src = oss.str();
    out.clearSymbols();

    std::regex structRe(R"(struct\s+([A-Za-z_]\w*))");
    for (std::sregex_iterator it(src.begin(), src.end(), structRe), end; it != end; ++it) {
        NodeDescription node;
        node.key = (*it)[1].str();
        node.kind = "Struct";
        node.name = node.key;
        node.type = "struct " + node.name;
        out.appendSymbol(node);
    }

    std::regex funcRe(R"(([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*\(([^)]*)\))");
    std::regex argRe(R"(([A-Za-z_]\w*)\s+([A-Za-z_]\w*))");
    for (std::sregex_iterator it(src.begin(), src.end(), funcRe), end; it != end; ++it) {
        NodeDescription node;
        node.kind = "Function";
        node.returnType = (*it)[1].str();
        node.name = (*it)[2].str();
        node.key = node.name;
        node.type = node.returnType;

        std::string argsText = (*it)[3].str();
        for (std::sregex_iterator ait(argsText.begin(), argsText.end(), argRe), aend; ait != aend; ++ait) {
            NodeDescription arg;
            arg.kind = "Parameter";
            arg.type = (*ait)[1].str();
            arg.name = (*ait)[2].str();
            arg.key = arg.name;
            node.appendChild(arg);
        }
        out.appendSymbol(node);
    }

    return true;
}

} // namespace

Mapper::Mapper() { index = clang_createIndex(0, 0); }
Mapper::~Mapper() { clang_disposeIndex(index); }

void Mapper::NodeDescription::clearChildren() {
    children = nullptr;
}

void Mapper::NodeDescription::appendChild(const NodeDescription& child) {
    CPtr<CLL<NodeDescription>> list = ensureNodeDescriptionList(children);
    CPtr<NodeDescription> value(child);
    CPtr<CLL<NodeDescription>> node(value);
    list->store(node, true);
}

size_t Mapper::NodeDescription::childCount() const {
    size_t count = 0;
    forEachChild([&](CPtr<NodeDescription>&) { ++count; });
    return count;
}

bool Mapper::NodeDescription::hasChildren() const {
    return static_cast<bool>(firstChild());
}

CPtr<NodeDescription> Mapper::NodeDescription::firstChild() const {
    CPtr<NodeDescription> first = nullptr;
    forEachChild([&](CPtr<NodeDescription>& child) {
        if (!first && child) {
            first = child;
        }
    });
    return first;
}

void Mapper::NodeDescription::forEachChild(const std::function<void(CPtr<NodeDescription>&)>& callback) const {
    if (!children) {
        return;
    }
    children->forEach([&](CPtr<NodeDescription>& child) {
        callback(child);
    }, false);
}

void Mapper::ParseDescription::clearSymbols() {
    symbols = nullptr;
}

void Mapper::ParseDescription::appendSymbol(const NodeDescription& symbol) {
    CPtr<CLL<NodeDescription>> list = ensureNodeDescriptionList(symbols);
    CPtr<NodeDescription> value(symbol);
    CPtr<CLL<NodeDescription>> node(value);
    list->store(node, true);
}

size_t Mapper::ParseDescription::symbolCount() const {
    size_t count = 0;
    forEachSymbol([&](CPtr<NodeDescription>&) { ++count; });
    return count;
}

bool Mapper::ParseDescription::hasSymbols() const {
    return static_cast<bool>(firstSymbol());
}

CPtr<NodeDescription> Mapper::ParseDescription::firstSymbol() const {
    CPtr<NodeDescription> first = nullptr;
    forEachSymbol([&](CPtr<NodeDescription>& symbol) {
        if (!first && symbol) {
            first = symbol;
        }
    });
    return first;
}

void Mapper::ParseDescription::forEachSymbol(const std::function<void(CPtr<NodeDescription>&)>& callback) const {
    if (!symbols) {
        return;
    }
    symbols->forEach([&](CPtr<NodeDescription>& symbol) {
        callback(symbol);
    }, false);
}

bool Mapper::parseDescription(const std::string& filename, ParseDescription& out) {
    CXTranslationUnit unit;
    const char* args[3] = {nullptr, nullptr, nullptr};
    int argCount = 0;
    auto ext = std::filesystem::path(filename).extension().string();
    if (ext == ".h" || ext == ".c") {
        args[0] = "-x";
        args[1] = "c";
        args[2] = "-std=c11";
        argCount = 3;
    } else if (ext == ".hpp" || ext == ".hh" || ext == ".cc" || ext == ".cpp" || ext == ".cxx") {
        args[0] = "-x";
        args[1] = "c++";
        args[2] = "-std=c++17";
        argCount = 3;
    }
    CXErrorCode err = clang_parseTranslationUnit2(
        index,
        filename.c_str(),
        argCount > 0 ? args : nullptr,
        argCount,
        nullptr,
        0,
        CXTranslationUnit_None,
        &unit
    );
    if (err != CXError_Success) {
        return parseDescriptionFallback(filename, out);
    }

    out.clearSymbols();
    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor parent, CXClientData clientData) {
            auto* description = static_cast<ParseDescription*>(clientData);
            const std::string kind = cursorKindName(clang_getCursorKind(child));
            if (kind.empty()) {
                return CXChildVisit_Recurse;
            }
            description->appendSymbol(describeCursor(child, parent));
            return CXChildVisit_Continue;
        },
        &out);
    clang_disposeTranslationUnit(unit);
    return true;
}

CPtr<ListreeValue> Mapper::materialize(const ParseDescription& description) {
    CPtr<ListreeValue> root = createNullValue();
    description.forEachSymbol([&](CPtr<NodeDescription>& symbol) {
        if (symbol) {
            addNamedItem(root, symbol->key, materializeNode(*symbol));
        }
    });
    bindTypes(root);
    return root;
}

CPtr<ListreeValue> Mapper::parse(const std::string& filename) {
    ParseDescription description;
    if (!parseDescription(filename, description)) {
        return nullptr;
    }
    return materialize(description);
}

} // namespace cartographer
} // namespace agentc
