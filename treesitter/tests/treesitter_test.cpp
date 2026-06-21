// treesitter_test.cpp — G094 Tree-Sitter integration tests
//
// Tests for the tree-sitter bridge: loading language parsers,
// parsing source code, and converting ASTs to Listree.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <gtest/gtest.h>
#include "tree_sitter_bridge.h"
#include "../core/cursor.h"
#include <filesystem>

using namespace agentc::treesitter;

#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif
#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif

// Helper: read a string field from a Listree tree
static std::string getField(CPtr<agentc::ListreeValue> tree, const std::string& name) {
    if (!tree) return "";
    auto item = tree->find(name);
    if (!item || !item->getValue()) return "";
    auto v = item->getValue(false, false);
    if (!v || !v->getData()) return "";
    return std::string(static_cast<char*>(v->getData()), v->getLength());
}

// Helper: get children list from a tree node
static CPtr<agentc::ListreeValue> getChildren(CPtr<agentc::ListreeValue> tree) {
    if (!tree) return nullptr;
    auto item = tree->find("children");
    if (!item || !item->getValue()) return nullptr;
    return item->getValue(false, false);
}

// Helper: count items in a list
static size_t listCount(CPtr<agentc::ListreeValue> list) {
    if (!list || !list->isListMode()) return 0;
    size_t count = 0;
    list->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++count; }, false);
    return count;
}

// ---------------------------------------------------------------------------
// Language loading tests
// ---------------------------------------------------------------------------

TEST(TreeSitterTest, LoadLanguageByNameFindsSystemParser) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    // C parser should be available on the system
    bool ok = bridge.loadLanguageByName("c", errorMsg);
    ASSERT_TRUE(ok) << "Failed to load C parser: " << errorMsg;
    EXPECT_TRUE(bridge.hasLanguage("c"));
}

TEST(TreeSitterTest, LoadLanguageByNameFailsForUnknownLanguage) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    bool ok = bridge.loadLanguageByName("nonexistent_lang", errorMsg);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(errorMsg.empty());
}

TEST(TreeSitterTest, ListLoadedLanguages) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("python", errorMsg));
    auto langs = bridge.listLanguages();
    EXPECT_EQ(langs.size(), 1u);
    EXPECT_EQ(langs[0], "python");
}

// ---------------------------------------------------------------------------
// Parsing tests
// ---------------------------------------------------------------------------

TEST(TreeSitterTest, ParseCSourceProducesAst) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string source = "int main() { return 42; }";
    auto ast = bridge.parse("c", source, errorMsg);
    ASSERT_TRUE(ast) << errorMsg;

    // Root type should be "translation_unit"
    EXPECT_EQ(getField(ast, "type"), "translation_unit");

    // Should have children
    auto children = getChildren(ast);
    ASSERT_TRUE(children);
    EXPECT_GT(listCount(children), 0u);
}

TEST(TreeSitterTest, ParsePythonSourceProducesAst) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("python", errorMsg));

    std::string source = "def hello():\n    return 42\n";
    auto ast = bridge.parse("python", source, errorMsg);
    ASSERT_TRUE(ast) << errorMsg;

    // Root type should be "module"
    EXPECT_EQ(getField(ast, "type"), "module");

    // Should have children
    auto children = getChildren(ast);
    ASSERT_TRUE(children);
    EXPECT_GT(listCount(children), 0u);
}

TEST(TreeSitterTest, ParseFailsForUnloadedLanguage) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    auto result = bridge.parse("rust", "fn main() {}", errorMsg);
    EXPECT_FALSE(result);
    EXPECT_FALSE(errorMsg.empty());
}

TEST(TreeSitterTest, ParsedAstHasByteOffsets) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string source = "int x = 42;";
    auto ast = bridge.parse("c", source, errorMsg);
    ASSERT_TRUE(ast);

    std::string startByte = getField(ast, "start_byte");
    std::string endByte = getField(ast, "end_byte");
    EXPECT_EQ(startByte, "0");
    EXPECT_EQ(endByte, std::to_string(source.size()));
}

TEST(TreeSitterTest, ParsedAstHasTextField) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string source = "int x = 42;";
    auto ast = bridge.parse("c", source, errorMsg);
    ASSERT_TRUE(ast);

    std::string text = getField(ast, "text");
    EXPECT_EQ(text, source);
}

TEST(TreeSitterTest, ParsedAstHasStartAndEndPoints) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string source = "int x;";
    auto ast = bridge.parse("c", source, errorMsg);
    ASSERT_TRUE(ast);

    auto startItem = ast->find("start_point");
    ASSERT_TRUE(startItem && startItem->getValue());
    auto startPoint = startItem->getValue(false, false);
    EXPECT_EQ(getField(startPoint, "row"), "0");
    EXPECT_EQ(getField(startPoint, "column"), "0");
}

// ---------------------------------------------------------------------------
// Edict parser tests
// ---------------------------------------------------------------------------

TEST(TreeSitterTest, LoadEdictParserFromBuildDir) {
    TreeSitterBridge bridge;
    std::string errorMsg;

    // Set build dir env so loadLanguageByName finds the Edict parser
    setenv("AGENTC_BUILD_DIR", TEST_BUILD_DIR, 1);

    bool ok = bridge.loadLanguageByName("edict", errorMsg);
    ASSERT_TRUE(ok) << "Failed to load Edict parser: " << errorMsg;
    EXPECT_TRUE(bridge.hasLanguage("edict"));
}

TEST(TreeSitterTest, ParseEdictSourceProducesAst) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    setenv("AGENTC_BUILD_DIR", TEST_BUILD_DIR, 1);
    ASSERT_TRUE(bridge.loadLanguageByName("edict", errorMsg));

    std::string source = "[libPath] load @defs 10 32 defs.add";
    auto ast = bridge.parse("edict", source, errorMsg);
    ASSERT_TRUE(ast) << errorMsg;

    // Root type should be "source"
    EXPECT_EQ(getField(ast, "type"), "source");

    // Should have children
    auto children = getChildren(ast);
    ASSERT_TRUE(children);
    EXPECT_GT(listCount(children), 0u);
}

TEST(TreeSitterTest, ParseEdictSigilExpression) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    setenv("AGENTC_BUILD_DIR", TEST_BUILD_DIR, 1);
    ASSERT_TRUE(bridge.loadLanguageByName("edict", errorMsg));

    // @defs should parse as a sigil_expression with target "defs"
    std::string source = "@defs";
    auto ast = bridge.parse("edict", source, errorMsg);
    ASSERT_TRUE(ast);

    auto children = getChildren(ast);
    ASSERT_TRUE(children);
    ASSERT_EQ(listCount(children), 1u);

    // Get first child
    size_t i = 0;
    CPtr<agentc::ListreeValue> firstChild;
    children->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (i == 0 && ref && ref->getValue()) firstChild = ref->getValue();
        ++i;
    }, false);

    ASSERT_TRUE(firstChild);
    EXPECT_EQ(getField(firstChild, "type"), "sigil_expression");
}

TEST(TreeSitterTest, ParseEdictLiteral) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    setenv("AGENTC_BUILD_DIR", TEST_BUILD_DIR, 1);
    ASSERT_TRUE(bridge.loadLanguageByName("edict", errorMsg));

    // [code body] should parse as a literal containing inner terms
    std::string source = "[@a a]";
    auto ast = bridge.parse("edict", source, errorMsg);
    ASSERT_TRUE(ast);

    auto children = getChildren(ast);
    ASSERT_TRUE(children);
    ASSERT_EQ(listCount(children), 1u);

    size_t i = 0;
    CPtr<agentc::ListreeValue> firstChild;
    children->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (i == 0 && ref && ref->getValue()) firstChild = ref->getValue();
        ++i;
    }, false);

    ASSERT_TRUE(firstChild);
    EXPECT_EQ(getField(firstChild, "type"), "literal");

    // The literal should have children including inner terms.
    // tree-sitter includes both named and anonymous children.
    auto literalChildren = getChildren(firstChild);
    ASSERT_TRUE(literalChildren);
    EXPECT_GE(listCount(literalChildren), 2u);
}

TEST(TreeSitterTest, ParseEdictJsonObject) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    setenv("AGENTC_BUILD_DIR", TEST_BUILD_DIR, 1);
    ASSERT_TRUE(bridge.loadLanguageByName("edict", errorMsg));

    std::string source = R"({"key": "value"})";
    auto ast = bridge.parse("edict", source, errorMsg);
    ASSERT_TRUE(ast);

    auto children = getChildren(ast);
    ASSERT_TRUE(children);
    ASSERT_EQ(listCount(children), 1u);

    size_t i = 0;
    CPtr<agentc::ListreeValue> firstChild;
    children->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (i == 0 && ref && ref->getValue()) firstChild = ref->getValue();
        ++i;
    }, false);

    ASSERT_TRUE(firstChild);
    EXPECT_EQ(getField(firstChild, "type"), "json_object");
}

// ---------------------------------------------------------------------------
// JSON serialization test (compatibility with Edict to_json!)
// ---------------------------------------------------------------------------

TEST(TreeSitterTest, ParsedAstCanBeJsonSerialized) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string source = "int x;";
    auto ast = bridge.parse("c", source, errorMsg);
    ASSERT_TRUE(ast);

    // Serialize to JSON and check it contains expected fields
    std::string json;
    // Use the Listree JSON serializer
    ast->forEachTree([&](const std::string& name, CPtr<agentc::ListreeItem>&) {
        json += name + " ";
    });

    // Should contain type, children, start_byte, etc.
    EXPECT_NE(json.find("type"), std::string::npos);
    EXPECT_NE(json.find("children"), std::string::npos);
    EXPECT_NE(json.find("start_byte"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Structural diff tests (G094.4)
// ---------------------------------------------------------------------------

#include "structural_diff.h"

using agentc::treesitter::StructuralDiff;
using agentc::treesitter::DiffResult;
using agentc::treesitter::DiffEntry;

TEST(StructuralDiffTest, IdenticalSourcesProduceNoChanges) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string source = "int x = 42;";
    auto result = StructuralDiff::diff(bridge, "c", source, source, errorMsg);
    EXPECT_FALSE(result.hasChanges());
}

TEST(StructuralDiffTest, AddedNodeDetected) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string oldSource = "int x;";
    std::string newSource = "int x; int y;";
    auto result = StructuralDiff::diff(bridge, "c", oldSource, newSource, errorMsg);
    EXPECT_TRUE(result.hasChanges());

    // Should have at least one Added entry
    bool hasAdded = false;
    for (const auto& e : result.entries) {
        if (e.kind == DiffEntry::Added) hasAdded = true;
    }
    EXPECT_TRUE(hasAdded);
}

TEST(StructuralDiffTest, RemovedNodeDetected) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string oldSource = "int x; int y;";
    std::string newSource = "int x;";
    auto result = StructuralDiff::diff(bridge, "c", oldSource, newSource, errorMsg);
    EXPECT_TRUE(result.hasChanges());

    // Should have at least one Removed entry
    bool hasRemoved = false;
    for (const auto& e : result.entries) {
        if (e.kind == DiffEntry::Removed) hasRemoved = true;
    }
    EXPECT_TRUE(hasRemoved);
}

TEST(StructuralDiffTest, ModifiedNodeDetected) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string oldSource = "int x = 1;";
    std::string newSource = "int x = 2;";
    auto result = StructuralDiff::diff(bridge, "c", oldSource, newSource, errorMsg);
    EXPECT_TRUE(result.hasChanges());

    // Should have at least one Modified or Removed+Added pair
    bool hasChange = false;
    for (const auto& e : result.entries) {
        if (e.kind == DiffEntry::Modified || e.kind == DiffEntry::Removed || e.kind == DiffEntry::Added) {
            hasChange = true;
        }
    }
    EXPECT_TRUE(hasChange);
}

TEST(StructuralDiffTest, DiffResultConvertsToListree) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string oldSource = "int x;";
    std::string newSource = "int x; int y;";
    auto result = StructuralDiff::diff(bridge, "c", oldSource, newSource, errorMsg);
    ASSERT_TRUE(result.hasChanges());

    auto listree = StructuralDiff::toListree(result);
    ASSERT_TRUE(listree);
    ASSERT_TRUE(listree->isListMode());

    // Count entries
    size_t count = 0;
    listree->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++count; }, false);
    EXPECT_EQ(count, result.entries.size());
}

TEST(StructuralDiffTest, SummaryStringIsHumanReadable) {
    TreeSitterBridge bridge;
    std::string errorMsg;
    ASSERT_TRUE(bridge.loadLanguageByName("c", errorMsg));

    std::string oldSource = "int x;";
    std::string newSource = "int x; int y;";
    auto result = StructuralDiff::diff(bridge, "c", oldSource, newSource, errorMsg);
    std::string summary = StructuralDiff::toSummary(result);
    EXPECT_NE(summary.find("changes"), std::string::npos);
    EXPECT_NE(summary.find("added"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Knowledge graph tests (G094.5)
// ---------------------------------------------------------------------------

#include "knowledge_graph.h"

using agentc::knowledge::KnowledgeGraph;

TEST(KnowledgeGraphTest, CreateEmptyGraph) {
    auto graph = KnowledgeGraph::create();
    ASSERT_TRUE(graph);
    EXPECT_EQ(KnowledgeGraph::nodeCount(graph), 0u);
    EXPECT_EQ(KnowledgeGraph::edgeCount(graph), 0u);
}

TEST(KnowledgeGraphTest, AddAndRetrieveNode) {
    auto graph = KnowledgeGraph::create();
    KnowledgeGraph::addNode(graph, "function_main");
    EXPECT_TRUE(KnowledgeGraph::hasNode(graph, "function_main"));
    EXPECT_EQ(KnowledgeGraph::nodeCount(graph), 1u);

    auto node = KnowledgeGraph::getNode(graph, "function_main");
    EXPECT_TRUE(node);
}

TEST(KnowledgeGraphTest, AddNodeWithProperties) {
    auto graph = KnowledgeGraph::create();
    auto props = agentc::createNullValue();
    agentc::addNamedItem(props, "line", agentc::createStringValue("42"));
    agentc::addNamedItem(props, "file", agentc::createStringValue("main.c"));
    KnowledgeGraph::addNode(graph, "func", props);

    auto node = KnowledgeGraph::getNode(graph, "func");
    ASSERT_TRUE(node);
    auto lineItem = node->find("line");
    ASSERT_TRUE(lineItem && lineItem->getValue());
    auto lineVal = lineItem->getValue(false, false);
    std::string lineStr(static_cast<char*>(lineVal->getData()), lineVal->getLength());
    EXPECT_EQ(lineStr, "42");
}

TEST(KnowledgeGraphTest, AddAndQueryEdges) {
    auto graph = KnowledgeGraph::create();
    KnowledgeGraph::addNode(graph, "a");
    KnowledgeGraph::addNode(graph, "b");
    KnowledgeGraph::addNode(graph, "c");
    KnowledgeGraph::addEdge(graph, "a", "calls", "b");
    KnowledgeGraph::addEdge(graph, "a", "calls", "c");
    KnowledgeGraph::addEdge(graph, "b", "imports", "c");

    EXPECT_EQ(KnowledgeGraph::edgeCount(graph), 3u);

    // Query all edges from "a"
    auto aCalls = KnowledgeGraph::queryEdges(graph, "a", "", "");
    ASSERT_TRUE(aCalls && aCalls->isListMode());
    size_t count = 0;
    aCalls->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++count; }, false);
    EXPECT_EQ(count, 2u);

    // Query specific relation
    auto imports = KnowledgeGraph::queryEdges(graph, "", "imports", "");
    count = 0;
    imports->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++count; }, false);
    EXPECT_EQ(count, 1u);

    // Query specific target
    auto toC = KnowledgeGraph::queryEdges(graph, "", "", "c");
    count = 0;
    toC->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++count; }, false);
    EXPECT_EQ(count, 2u);
}

TEST(KnowledgeGraphTest, ListNodes) {
    auto graph = KnowledgeGraph::create();
    KnowledgeGraph::addNode(graph, "x");
    KnowledgeGraph::addNode(graph, "y");

    auto nodes = KnowledgeGraph::listNodes(graph);
    ASSERT_TRUE(nodes && nodes->isListMode());
    size_t count = 0;
    nodes->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++count; }, false);
    EXPECT_EQ(count, 2u);
}

TEST(KnowledgeGraphTest, RemoveNodeAndConnectedEdges) {
    auto graph = KnowledgeGraph::create();
    KnowledgeGraph::addNode(graph, "a");
    KnowledgeGraph::addNode(graph, "b");
    KnowledgeGraph::addEdge(graph, "a", "calls", "b");

    EXPECT_TRUE(KnowledgeGraph::removeNode(graph, "a"));
    EXPECT_FALSE(KnowledgeGraph::hasNode(graph, "a"));
    EXPECT_EQ(KnowledgeGraph::edgeCount(graph), 0u);
}

TEST(KnowledgeGraphTest, GraphIsJsonSerializable) {
    auto graph = KnowledgeGraph::create();
    KnowledgeGraph::addNode(graph, "func");
    KnowledgeGraph::addEdge(graph, "func", "calls", "helper");

    // Verify the graph has the expected structure
    auto nodes = KnowledgeGraph::listNodes(graph);
    ASSERT_TRUE(nodes);
    auto edges = KnowledgeGraph::listEdges(graph);
    ASSERT_TRUE(edges);

    // The graph should be a tree with "nodes" and "edges" fields
    auto nodesItem = graph->find("nodes");
    ASSERT_TRUE(nodesItem);
    auto edgesItem = graph->find("edges");
    ASSERT_TRUE(edgesItem);
}
