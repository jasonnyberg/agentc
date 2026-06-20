// tree_sitter_bridge.h — G094 Tree-Sitter AST Bridge
//
// Provides a C++ bridge between libtree-sitter and the Edict VM.
// Loads language parsers from shared libraries (libtree-sitter-<lang>.so),
// parses source code into Listree trees, and exposes query operations
// for agent-level AST navigation.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include "../listree/listree.h"
#include "../core/cursor.h"

// Forward-declare TSLanguage to avoid pulling in the full tree-sitter API header here.
struct TSLanguage;
typedef struct TSLanguage TSLanguage;

namespace agentc {
namespace treesitter {

/// A loaded tree-sitter parser for a specific language.
class LanguageParser {
public:
    LanguageParser(const std::string& languageName, const TSLanguage* language);
    ~LanguageParser();

    /// Parse source code and return a Listree tree representation.
    /// The returned tree has:
    /// - Named child nodes as tree-mode ListreeValue items with the node type as name.
    /// - Each node has: "type" (string), "start_byte" (string), "end_byte" (string),
    ///   "start_point" (tree with "row"/"column"), "end_point" (tree with "row"/"column"),
    ///   and "children" (list of child nodes).
    /// - Anonymous tokens are included as list items in a "tokens" list.
    CPtr<ListreeValue> parse(const std::string& source) const;

    /// Parse source and return the S-expression representation as a string.
    std::string parseToSExpr(const std::string& source) const;

    const std::string& getLanguageName() const { return languageName_; }
    const TSLanguage* getLanguage() const { return language_; }

private:
    std::string languageName_;
    const TSLanguage* language_;

    CPtr<ListreeValue> nodeToListree(const void* node, const std::string& source) const;
};

/// Manages loading and caching tree-sitter language parsers.
/// Parsers are loaded from shared libraries via dlopen.
class TreeSitterBridge {
public:
    TreeSitterBridge();
    ~TreeSitterBridge();

    /// Load a language parser from a shared library.
    /// The library must export `tree_sitter_<language>`.
    /// Returns true on success, false on failure (with errorMsg set).
    bool loadLanguage(const std::string& libraryPath, const std::string& languageName,
                      std::string& errorMsg);

    /// Load a language by name, searching standard library paths.
    /// Tries /usr/lib64/libtree-sitter-<name>.so* and /usr/lib/libtree-sitter-<name>.so*
    bool loadLanguageByName(const std::string& languageName, std::string& errorMsg);

    /// Check if a language is loaded.
    bool hasLanguage(const std::string& languageName) const;

    /// Get a loaded parser. Returns nullptr if not loaded.
    std::shared_ptr<LanguageParser> getParser(const std::string& languageName) const;

    /// Parse source code with the specified language.
    /// Returns a Listree tree or nullptr on error.
    CPtr<ListreeValue> parse(const std::string& languageName,
                             const std::string& source,
                             std::string& errorMsg) const;

    /// List all loaded language names.
    std::vector<std::string> listLanguages() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace treesitter
} // namespace agentc
