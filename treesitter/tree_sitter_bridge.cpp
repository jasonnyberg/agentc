// tree_sitter_bridge.cpp — G094 Tree-Sitter AST Bridge implementation
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "tree_sitter_bridge.h"
#include <dlfcn.h>
#include <tree_sitter/api.h>
#include <sstream>
#include <filesystem>

namespace agentc {
namespace treesitter {

// ---------------------------------------------------------------------------
// LanguageParser
// ---------------------------------------------------------------------------

LanguageParser::LanguageParser(const std::string& languageName, const TSLanguage* language)
    : languageName_(languageName), language_(language) {}

LanguageParser::~LanguageParser() = default;

// Helper: create a tree-mode ListreeValue with named string field
static CPtr<ListreeValue> makeTreeWithField(const char* key, const std::string& value) {
    auto tree = createNullValue();
    addNamedItem(tree, key, createStringValue(value));
    return tree;
}

static CPtr<ListreeValue> pointToTree(uint32_t row, uint32_t col) {
    auto tree = createNullValue();
    addNamedItem(tree, "row", createStringValue(std::to_string(row)));
    addNamedItem(tree, "column", createStringValue(std::to_string(col)));
    return tree;
}

CPtr<ListreeValue> LanguageParser::nodeToListree(const void* nodePtr, const std::string& source) const {
    TSNode node = *static_cast<const TSNode*>(nodePtr);
    auto result = createNullValue();

    // Type
    const char* type = ts_node_type(node);
    addNamedItem(result, "type", createStringValue(type));

    // Byte range
    uint32_t startByte = ts_node_start_byte(node);
    uint32_t endByte = ts_node_end_byte(node);
    addNamedItem(result, "start_byte", createStringValue(std::to_string(startByte)));
    addNamedItem(result, "end_byte", createStringValue(std::to_string(endByte)));

    // Text snippet
    if (endByte <= source.size() && endByte > startByte) {
        addNamedItem(result, "text", createStringValue(source.substr(startByte, endByte - startByte)));
    }

    // Start/end points
    TSPoint startPoint = ts_node_start_point(node);
    TSPoint endPoint = ts_node_end_point(node);
    addNamedItem(result, "start_point", pointToTree(startPoint.row, startPoint.column));
    addNamedItem(result, "end_point", pointToTree(endPoint.row, endPoint.column));

    // Named children
    uint32_t childCount = ts_node_child_count(node);
    uint32_t namedChildCount = ts_node_named_child_count(node);

    auto childrenList = createListValue();
    for (uint32_t i = 0; i < childCount; ++i) {
        TSNode child = ts_node_child(node, i);
        // Create a child tree node
        auto childTree = nodeToListree(&child, source);
        // For named children, add the field name if available
        const char* fieldName = ts_node_field_name_for_child(node, i);
        if (fieldName) {
            addNamedItem(childTree, "field", createStringValue(fieldName));
        }
        addListItem(childrenList, childTree);
    }
    addNamedItem(result, "children", childrenList);

    // Is named? (vs anonymous token)
    addNamedItem(result, "is_named", createStringValue(ts_node_is_named(node) ? "true" : "false"));

    // Is error?
    if (ts_node_is_error(node)) {
        addNamedItem(result, "is_error", createStringValue("true"));
    }

    return result;
}

CPtr<ListreeValue> LanguageParser::parse(const std::string& source) const {
    if (!language_) return nullptr;

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, language_);

    TSInput input;
    input.payload = const_cast<std::string*>(&source);
    input.read = [](void* payload, uint32_t byte_offset, TSPoint, uint32_t* bytes_read) -> const char* {
        auto* src = static_cast<std::string*>(payload);
        if (byte_offset >= src->size()) {
            *bytes_read = 0;
            return nullptr;
        }
        *bytes_read = static_cast<uint32_t>(src->size() - byte_offset);
        return src->data() + byte_offset;
    };
    input.encoding = TSInputEncodingUTF8;

    TSTree* tree = ts_parser_parse(parser, nullptr, input);
    if (!tree) {
        ts_parser_delete(parser);
        return nullptr;
    }

    TSNode root = ts_tree_root_node(tree);
    auto result = nodeToListree(&root, source);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return result;
}

std::string LanguageParser::parseToSExpr(const std::string& source) const {
    if (!language_) return "";

    TSParser* parser = ts_parser_new();
    ts_parser_set_language(parser, language_);

    TSTree* tree = ts_parser_parse_string(parser, nullptr,
        reinterpret_cast<const char*>(source.data()),
        static_cast<uint32_t>(source.size()));

    if (!tree) {
        ts_parser_delete(parser);
        return "";
    }

    TSNode root = ts_tree_root_node(tree);
    char* sexpr = ts_node_string(root);
    std::string result(sexpr);
    free(sexpr);

    ts_tree_delete(tree);
    ts_parser_delete(parser);
    return result;
}

// ---------------------------------------------------------------------------
// TreeSitterBridge
// ---------------------------------------------------------------------------

struct TreeSitterBridge::Impl {
    // Loaded language parsers by name
    std::unordered_map<std::string, std::shared_ptr<LanguageParser>> parsers;
    // dlopen handles for cleanup
    std::vector<std::pair<void*, std::string>> handles;

    ~Impl() {
        for (auto& [handle, name] : handles) {
            if (handle) dlclose(handle);
        }
    }
};

TreeSitterBridge::TreeSitterBridge() : impl_(std::make_unique<Impl>()) {}
TreeSitterBridge::~TreeSitterBridge() = default;

bool TreeSitterBridge::loadLanguage(const std::string& libraryPath,
                                     const std::string& languageName,
                                     std::string& errorMsg) {
    void* handle = dlopen(libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        errorMsg = "dlopen failed: " + std::string(dlerror());
        return false;
    }

    // Construct symbol name: tree_sitter_<language>
    std::string symbolName = "tree_sitter_" + languageName;
    auto languageFunc = reinterpret_cast<const TSLanguage* (*)()>(
        dlsym(handle, symbolName.c_str()));

    if (!languageFunc) {
        errorMsg = "Symbol not found: " + symbolName + " (" + std::string(dlerror()) + ")";
        dlclose(handle);
        return false;
    }

    const TSLanguage* language = languageFunc();
    if (!language) {
        errorMsg = "tree_sitter_" + languageName + "() returned null";
        dlclose(handle);
        return false;
    }

    auto parser = std::make_shared<LanguageParser>(languageName, language);
    impl_->parsers[languageName] = parser;
    impl_->handles.push_back({handle, languageName});
    return true;
}

bool TreeSitterBridge::loadLanguageByName(const std::string& languageName,
                                            std::string& errorMsg) {
    // Check if already loaded
    if (impl_->parsers.find(languageName) != impl_->parsers.end()) {
        return true;
    }

    // Search standard paths for libtree-sitter-<name>.so*
    std::vector<std::string> searchPaths = {
        "/usr/lib64",
        "/usr/lib",
        "/usr/local/lib64",
        "/usr/local/lib",
    };

    // Try exact .so and versioned .so.N
    for (const auto& dir : searchPaths) {
        std::filesystem::path base(dir);
        // Try libtree-sitter-<name>.so
        std::string libName = "libtree-sitter-" + languageName + ".so";
        std::filesystem::path libPath = base / libName;
        if (std::filesystem::exists(libPath)) {
            return loadLanguage(libPath.string(), languageName, errorMsg);
        }

        // Try globbing for versioned .so.N
        if (std::filesystem::exists(base)) {
            for (const auto& entry : std::filesystem::directory_iterator(base)) {
                std::string filename = entry.path().filename().string();
                std::string prefix = "libtree-sitter-" + languageName + ".so.";
                if (filename.substr(0, prefix.size()) == prefix) {
                    return loadLanguage(entry.path().string(), languageName, errorMsg);
                }
            }
        }
    }

    // Also try the build directory for the Edict parser
    const char* buildDir = std::getenv("AGENTC_BUILD_DIR");
    if (buildDir) {
        std::filesystem::path edictLib = std::filesystem::path(buildDir) / "treesitter" / "libtree-sitter-edict.so";
        if (languageName == "edict" && std::filesystem::exists(edictLib)) {
            return loadLanguage(edictLib.string(), languageName, errorMsg);
        }
    }

    errorMsg = "Language library not found for: " + languageName;
    return false;
}

bool TreeSitterBridge::hasLanguage(const std::string& languageName) const {
    return impl_->parsers.find(languageName) != impl_->parsers.end();
}

std::shared_ptr<LanguageParser> TreeSitterBridge::getParser(const std::string& languageName) const {
    auto it = impl_->parsers.find(languageName);
    if (it == impl_->parsers.end()) return nullptr;
    return it->second;
}

CPtr<ListreeValue> TreeSitterBridge::parse(const std::string& languageName,
                                             const std::string& source,
                                             std::string& errorMsg) const {
    auto parser = getParser(languageName);
    if (!parser) {
        errorMsg = "Language not loaded: " + languageName;
        return nullptr;
    }
    return parser->parse(source);
}

std::vector<std::string> TreeSitterBridge::listLanguages() const {
    std::vector<std::string> result;
    for (const auto& [name, _] : impl_->parsers) {
        result.push_back(name);
    }
    return result;
}

} // namespace treesitter
} // namespace agentc
