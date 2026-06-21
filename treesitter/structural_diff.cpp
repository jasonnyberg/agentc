// structural_diff.cpp — G094.4 Structural Diff Engine implementation
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "structural_diff.h"
#include <algorithm>
#include <sstream>

namespace agentc {
namespace treesitter {

// Helper: read a string field from a Listree tree node
static std::string getField(CPtr<ListreeValue> tree, const std::string& name) {
    if (!tree) return "";
    auto item = tree->find(name);
    if (!item || !item->getValue()) return "";
    auto v = item->getValue(false, false);
    if (!v || !v->getData()) return "";
    return std::string(static_cast<char*>(v->getData()), v->getLength());
}

// Helper: get children list from a tree node
static CPtr<ListreeValue> getChildren(CPtr<ListreeValue> tree) {
    if (!tree) return nullptr;
    auto item = tree->find("children");
    if (!item || !item->getValue()) return nullptr;
    return item->getValue(false, false);
}

// Helper: count list items
static size_t listCount(CPtr<ListreeValue> list) {
    if (!list || !list->isListMode()) return 0;
    size_t count = 0;
    list->forEachList([&](CPtr<ListreeValueRef>&) { ++count; }, false);
    return count;
}

// Helper: get list item at index (insertion order)
static CPtr<ListreeValue> listAt(CPtr<ListreeValue> list, size_t index) {
    if (!list || !list->isListMode()) return nullptr;
    size_t current = 0;
    CPtr<ListreeValue> result;
    list->forEachList([&](CPtr<ListreeValueRef>& ref) {
        if (result || !ref || !ref->getValue()) return;
        if (current == index) {
            result = ref->getValue();
            return;
        }
        ++current;
    }, false);
    return result;
}

std::vector<StructuralDiff::FlatNode> StructuralDiff::flattenAst(
        CPtr<ListreeValue> ast, const std::string& source) {
    std::vector<FlatNode> result;
    if (!ast) return result;

    // Only include named nodes
    std::string isNamed = getField(ast, "is_named");
    if (isNamed == "true") {
        FlatNode node;
        node.type = getField(ast, "type");
        node.text = getField(ast, "text");
        node.startByte = static_cast<uint32_t>(std::stoul(getField(ast, "start_byte")));
        node.endByte = static_cast<uint32_t>(std::stoul(getField(ast, "end_byte")));

        // Parse start point row
        auto startPointItem = ast->find("start_point");
        if (startPointItem && startPointItem->getValue()) {
            auto startPoint = startPointItem->getValue(false, false);
            node.startRow = static_cast<uint32_t>(std::stoul(getField(startPoint, "row")));
        } else {
            node.startRow = 0;
        }
        node.isNamed = true;
        result.push_back(node);
    }

    // Recurse into children
    auto children = getChildren(ast);
    size_t count = listCount(children);
    for (size_t i = 0; i < count; ++i) {
        auto child = listAt(children, i);
        auto childNodes = flattenAst(child, source);
        result.insert(result.end(), childNodes.begin(), childNodes.end());
    }

    return result;
}

DiffResult StructuralDiff::computeDiff(const std::vector<FlatNode>& oldNodes,
                                         const std::vector<FlatNode>& newNodes) {
    DiffResult result;

    // Simple LCS-based diff: match nodes by type+text, identify adds/removes/modifications
    // Build a matrix for LCS
    size_t m = oldNodes.size();
    size_t n = newNodes.size();

    // LCS DP table
    std::vector<std::vector<int>> lcs(m + 1, std::vector<int>(n + 1, 0));
    for (int i = m - 1; i >= 0; --i) {
        for (int j = n - 1; j >= 0; --j) {
            if (oldNodes[i].type == newNodes[j].type && oldNodes[i].text == newNodes[j].text) {
                lcs[i][j] = lcs[i + 1][j + 1] + 1;
            } else {
                lcs[i][j] = std::max(lcs[i + 1][j], lcs[i][j + 1]);
            }
        }
    }

    // Backtrack to produce diff
    size_t i = 0, j = 0;
    while (i < m && j < n) {
        if (oldNodes[i].type == newNodes[j].type && oldNodes[i].text == newNodes[j].text) {
            // Match — check if position changed (Moved)
            if (oldNodes[i].startByte != newNodes[j].startByte ||
                oldNodes[i].startRow != newNodes[j].startRow) {
                DiffEntry entry;
                entry.kind = DiffEntry::Moved;
                entry.nodeType = oldNodes[i].type;
                entry.oldText = oldNodes[i].text;
                entry.newText = newNodes[j].text;
                entry.oldStartByte = oldNodes[i].startByte;
                entry.newStartByte = newNodes[j].startByte;
                entry.oldStartRow = oldNodes[i].startRow;
                entry.newStartRow = newNodes[j].startRow;
                result.entries.push_back(entry);
            }
            ++i; ++j;
        } else if (lcs[i + 1][j] >= lcs[i][j + 1]) {
            // oldNodes[i] is removed
            DiffEntry entry;
            entry.kind = DiffEntry::Removed;
            entry.nodeType = oldNodes[i].type;
            entry.oldText = oldNodes[i].text;
            entry.oldStartByte = oldNodes[i].startByte;
            entry.oldStartRow = oldNodes[i].startRow;
            entry.newStartByte = 0;
            entry.newStartRow = 0;
            result.entries.push_back(entry);
            ++i;
        } else {
            // newNodes[j] is added
            DiffEntry entry;
            entry.kind = DiffEntry::Added;
            entry.nodeType = newNodes[j].type;
            entry.newText = newNodes[j].text;
            entry.newStartByte = newNodes[j].startByte;
            entry.newStartRow = newNodes[j].startRow;
            entry.oldStartByte = 0;
            entry.oldStartRow = 0;
            result.entries.push_back(entry);
            ++j;
        }
    }

    // Remaining removals
    while (i < m) {
        DiffEntry entry;
        entry.kind = DiffEntry::Removed;
        entry.nodeType = oldNodes[i].type;
        entry.oldText = oldNodes[i].text;
        entry.oldStartByte = oldNodes[i].startByte;
        entry.oldStartRow = oldNodes[i].startRow;
        result.entries.push_back(entry);
        ++i;
    }

    // Remaining additions
    while (j < n) {
        DiffEntry entry;
        entry.kind = DiffEntry::Added;
        entry.nodeType = newNodes[j].type;
        entry.newText = newNodes[j].text;
        entry.newStartByte = newNodes[j].startByte;
        entry.newStartRow = newNodes[j].startRow;
        result.entries.push_back(entry);
        ++j;
    }

    // Post-pass: detect Modified nodes (same type, different text, adjacent in diff)
    // This is a heuristic: if a Removed and Added entry are adjacent with the same type,
    // merge them into a Modified entry.
    std::vector<DiffEntry> merged;
    for (size_t k = 0; k < result.entries.size(); ++k) {
        if (k + 1 < result.entries.size() &&
            result.entries[k].kind == DiffEntry::Removed &&
            result.entries[k + 1].kind == DiffEntry::Added &&
            result.entries[k].nodeType == result.entries[k + 1].nodeType) {
            DiffEntry entry;
            entry.kind = DiffEntry::Modified;
            entry.nodeType = result.entries[k].nodeType;
            entry.oldText = result.entries[k].oldText;
            entry.newText = result.entries[k + 1].newText;
            entry.oldStartByte = result.entries[k].oldStartByte;
            entry.newStartByte = result.entries[k + 1].newStartByte;
            entry.oldStartRow = result.entries[k].oldStartRow;
            entry.newStartRow = result.entries[k + 1].newStartRow;
            merged.push_back(entry);
            ++k; // skip the Added entry
        } else {
            merged.push_back(result.entries[k]);
        }
    }
    result.entries = std::move(merged);

    return result;
}

DiffResult StructuralDiff::diff(TreeSitterBridge& bridge,
                                  const std::string& languageName,
                                  const std::string& oldSource,
                                  const std::string& newSource,
                                  std::string& errorMsg) {
    DiffResult result;

    auto oldAst = bridge.parse(languageName, oldSource, errorMsg);
    if (!oldAst) return result;

    auto newAst = bridge.parse(languageName, newSource, errorMsg);
    if (!newAst) return result;

    auto oldNodes = flattenAst(oldAst, oldSource);
    auto newNodes = flattenAst(newAst, newSource);

    return computeDiff(oldNodes, newNodes);
}

CPtr<ListreeValue> StructuralDiff::toListree(const DiffResult& result) {
    auto list = createListValue();
    for (const auto& entry : result.entries) {
        auto tree = createNullValue();

        const char* kindStr = "modified";
        switch (entry.kind) {
            case DiffEntry::Added:   kindStr = "added"; break;
            case DiffEntry::Removed: kindStr = "removed"; break;
            case DiffEntry::Modified: kindStr = "modified"; break;
            case DiffEntry::Moved:   kindStr = "moved"; break;
        }
        addNamedItem(tree, "kind", createStringValue(kindStr));
        addNamedItem(tree, "type", createStringValue(entry.nodeType));
        if (!entry.oldText.empty())
            addNamedItem(tree, "old_text", createStringValue(entry.oldText));
        if (!entry.newText.empty())
            addNamedItem(tree, "new_text", createStringValue(entry.newText));
        addNamedItem(tree, "old_start_byte", createStringValue(std::to_string(entry.oldStartByte)));
        addNamedItem(tree, "new_start_byte", createStringValue(std::to_string(entry.newStartByte)));
        addNamedItem(tree, "old_start_row", createStringValue(std::to_string(entry.oldStartRow)));
        addNamedItem(tree, "new_start_row", createStringValue(std::to_string(entry.newStartRow)));

        addListItem(list, tree);
    }
    return list;
}

std::string StructuralDiff::toSummary(const DiffResult& result) {
    std::ostringstream oss;
    int added = 0, removed = 0, modified = 0, moved = 0;
    for (const auto& e : result.entries) {
        switch (e.kind) {
            case DiffEntry::Added: ++added; break;
            case DiffEntry::Removed: ++removed; break;
            case DiffEntry::Modified: ++modified; break;
            case DiffEntry::Moved: ++moved; break;
        }
    }
    oss << result.entries.size() << " changes: "
        << added << " added, " << removed << " removed, "
        << modified << " modified, " << moved << " moved";
    return oss.str();
}

} // namespace treesitter
} // namespace agentc
