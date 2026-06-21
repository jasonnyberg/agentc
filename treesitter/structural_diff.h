// structural_diff.h — G094.4 Structural Diff Engine
//
// Compares two source code strings by parsing them into tree-sitter ASTs
// and producing a structured diff of node-level changes.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <string>
#include <vector>
#include "../listree/listree.h"
#include "tree_sitter_bridge.h"

namespace agentc {
namespace treesitter {

/// A single structural change between two ASTs.
struct DiffEntry {
    enum Kind {
        Added,      // Node exists only in the new tree
        Removed,    // Node exists only in the old tree
        Modified,   // Node exists in both but text/content differs
        Moved,      // Node exists in both, same content, different position
    };

    Kind kind;
    std::string nodeType;       // tree-sitter node type
    std::string oldText;        // text in old source (empty for Added)
    std::string newText;        // text in new source (empty for Removed)
    uint32_t oldStartByte;      // byte offset in old source (0 for Added)
    uint32_t newStartByte;      // byte offset in new source (0 for Removed)
    uint32_t oldStartRow;       // row in old source (0 for Added)
    uint32_t newStartRow;       // row in new source (0 for Removed)
};

/// Result of a structural diff.
struct DiffResult {
    std::vector<DiffEntry> entries;
    bool hasChanges() const { return !entries.empty(); }
};

/// Performs structural diffing between two source files using tree-sitter ASTs.
/// The diff is computed by comparing named nodes in the old and new ASTs
/// using a type+text matching algorithm.
class StructuralDiff {
public:
    /// Diff two source strings using the specified language.
    /// Returns a DiffResult with entries for each detected change.
    static DiffResult diff(TreeSitterBridge& bridge,
                           const std::string& languageName,
                           const std::string& oldSource,
                           const std::string& newSource,
                           std::string& errorMsg);

    /// Convert a DiffResult to a Listree list value for Edict consumption.
    /// Each entry is a tree with: kind, type, old_text, new_text,
    /// old_start_byte, new_start_byte, old_start_row, new_start_row.
    static CPtr<ListreeValue> toListree(const DiffResult& result);

    /// Convert a DiffResult to a human-readable summary string.
    static std::string toSummary(const DiffResult& result);

private:
    /// Represents a flattened AST node for comparison.
    struct FlatNode {
        std::string type;
        std::string text;
        uint32_t startByte;
        uint32_t endByte;
        uint32_t startRow;
        bool isNamed;
    };

    /// Flatten an AST (Listree tree from TreeSitterBridge::parse) into
    /// a vector of named nodes in depth-first order.
    static std::vector<FlatNode> flattenAst(CPtr<ListreeValue> ast,
                                             const std::string& source);

    /// Compute diff between two flattened node lists using LCS-based matching.
    static DiffResult computeDiff(const std::vector<FlatNode>& oldNodes,
                                   const std::vector<FlatNode>& newNodes);
};

} // namespace treesitter
} // namespace agentc
