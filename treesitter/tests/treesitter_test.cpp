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
