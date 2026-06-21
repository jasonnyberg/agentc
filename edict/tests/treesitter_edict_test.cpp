// treesitter_edict_test.cpp — G094 Edict-level tree-sitter integration tests
//
// Tests that Edict code can use the `treesitter` builtin capsule to
// load language parsers and parse source code into Listree ASTs.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <gtest/gtest.h>
#include "../edict_vm.h"
#include "../edict_compiler.h"
#include <cstdlib>

using namespace agentc::edict;

class TreeSitterEdictTest : public ::testing::Test {
protected:
    void SetUp() override {
        const char* buildDir = std::getenv("AGENTC_BUILD_DIR");
        if (!buildDir) {
#ifdef TEST_ROOT_BUILD_DIR
            setenv("AGENTC_BUILD_DIR", TEST_ROOT_BUILD_DIR, 1);
#endif
        }
    }

    std::string executeAndPopString(EdictVM& vm, EdictCompiler& compiler, const std::string& source) {
        int state = vm.execute(compiler.compile(source));
        if (state & VM_ERROR) return "ERROR: " + vm.getError();
        auto top = vm.popData();
        if (!top || !top->getData()) return "";
        return std::string(static_cast<const char*>(top->getData()), top->getLength());
    }
};

#ifndef TEST_ROOT_BUILD_DIR
#define TEST_ROOT_BUILD_DIR "."
#endif

TEST_F(TreeSitterEdictTest, TreesitterBuiltinIsAvailable) {
    EdictVM vm;
    EdictCompiler compiler;
    auto result = executeAndPopString(vm, compiler, "treesitter");
    EXPECT_NE(result.find("ERROR"), 0);
}

TEST_F(TreeSitterEdictTest, LoadSystemLanguageFromEdict) {
    EdictVM vm;
    EdictCompiler compiler;
    std::string result = executeAndPopString(vm, compiler, "'c treesitter.load!");
    EXPECT_EQ(result, "true");
}

TEST_F(TreeSitterEdictTest, ListLoadedLanguagesFromEdict) {
    EdictVM vm;
    EdictCompiler compiler;
    vm.execute(compiler.compile("'c treesitter.load! 'python treesitter.load!"));
    auto result = executeAndPopString(vm, compiler, "treesitter.list! to_json!");
    EXPECT_NE(result.find("c"), std::string::npos);
    EXPECT_NE(result.find("python"), std::string::npos);
}

TEST_F(TreeSitterEdictTest, ParseCFromEdict) {
    EdictVM vm;
    EdictCompiler compiler;
    vm.execute(compiler.compile("'c treesitter.load!"));

    // Use double-quoted strings for source code — the Edict compiler
    // strips the quotes and pushes the bare string value.
    std::string src = "'c \"int x;\" treesitter.parse! @ast ast.type";
    std::string result = executeAndPopString(vm, compiler, src);
    EXPECT_EQ(result, "translation_unit");
}

TEST_F(TreeSitterEdictTest, ParseEdictFromEdict) {
    EdictVM vm;
    EdictCompiler compiler;
    setenv("AGENTC_BUILD_DIR", TEST_ROOT_BUILD_DIR, 1);
    vm.execute(compiler.compile("'edict treesitter.load!"));

    // Use double-quoted string for multi-word Edict source
    std::string src = "'edict \"load @defs\" treesitter.parse! @ast ast.type";
    std::string result = executeAndPopString(vm, compiler, src);
    EXPECT_EQ(result, "source");
}

TEST_F(TreeSitterEdictTest, ParsedAstHasChildrenList) {
    EdictVM vm;
    EdictCompiler compiler;
    vm.execute(compiler.compile("'c treesitter.load!"));

    vm.execute(compiler.compile("'c \"int x;\" treesitter.parse! @ast"));
    auto state = vm.execute(compiler.compile("ast.children"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
    auto children = vm.popData();
    ASSERT_TRUE(children);
    // The "children" field is a list stored as the value of a named item.
    // When accessed via dotted path (ast.children), it returns the list value.
    if (children->isListMode()) {
        size_t count = 0;
        children->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++count; }, false);
        EXPECT_GE(count, 1u);
    } else {
        // If accessed as a tree, look for the named item
        auto item = children->find("children");
        if (item && item->getValue()) {
            auto innerList = item->getValue(false, false);
            ASSERT_TRUE(innerList);
            ASSERT_TRUE(innerList->isListMode());
            size_t count = 0;
            innerList->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++count; }, false);
            EXPECT_GE(count, 1u);
        }
    }
}

TEST_F(TreeSitterEdictTest, ParsedAstCanBeJsonSerialized) {
    EdictVM vm;
    EdictCompiler compiler;
    vm.execute(compiler.compile("'c treesitter.load!"));

    std::string src = "'c \"int x;\" treesitter.parse! to_json!";
    std::string result = executeAndPopString(vm, compiler, src);
    EXPECT_NE(result.find("type"), std::string::npos);
    EXPECT_NE(result.find("translation_unit"), std::string::npos);
}

TEST_F(TreeSitterEdictTest, StructuralDiffFromEdict) {
    EdictVM vm;
    EdictCompiler compiler;
    vm.execute(compiler.compile("'c treesitter.load!"));

    // Diff two C source strings — the result should be a non-empty list
    std::string src = "'c \"int x;\" \"int x; int y;\" treesitter.diff! @d d to_json!";
    std::string result = executeAndPopString(vm, compiler, src);
    // Should contain diff entries (added)
    EXPECT_NE(result.find("added"), std::string::npos);
}

TEST_F(TreeSitterEdictTest, StructuralDiffIdenticalNoChanges) {
    EdictVM vm;
    EdictCompiler compiler;
    vm.execute(compiler.compile("'c treesitter.load!"));

    // Diff identical sources — result should be an empty array
    std::string src = "'c \"int x;\" \"int x;\" treesitter.diff! to_json!";
    std::string result = executeAndPopString(vm, compiler, src);
    EXPECT_EQ(result, "[]");
}

// ---------------------------------------------------------------------------
// Knowledge graph Edict-level tests (G094.5)
// ---------------------------------------------------------------------------

TEST_F(TreeSitterEdictTest, KnowledgeGraphCreateAndAddNode) {
    EdictVM vm;
    EdictCompiler compiler;

    // Create graph, add a node, list nodes
    std::string src = "kgraph.create! @g g 'main_func kgraph.add_node! g kgraph.nodes! to_json!";
    std::string result = executeAndPopString(vm, compiler, src);
    EXPECT_NE(result.find("main_func"), std::string::npos);
}

TEST_F(TreeSitterEdictTest, KnowledgeGraphAddEdgeAndQuery) {
    EdictVM vm;
    EdictCompiler compiler;

    // Create graph, add nodes, add edge, query
    vm.execute(compiler.compile(
        "kgraph.create! @g "
        "g 'a kgraph.add_node! "
        "g 'b kgraph.add_node! "
        "g 'a 'calls 'b kgraph.add_edge!"));

    auto result = executeAndPopString(vm, compiler,
        "g 'a 'calls 'b kgraph.query! to_json!");
    EXPECT_NE(result.find("calls"), std::string::npos);
    EXPECT_NE(result.find("\"a\""), std::string::npos);
    EXPECT_NE(result.find("\"b\""), std::string::npos);
}

TEST_F(TreeSitterEdictTest, KnowledgeGraphGetNode) {
    EdictVM vm;
    EdictCompiler compiler;

    vm.execute(compiler.compile(
        "kgraph.create! @g g 'myfunc kgraph.add_node!"));

    // Get the node — should be non-null
    std::string result = executeAndPopString(vm, compiler,
        "g 'myfunc kgraph.get_node! to_json!");
    // A null node would be "null", a real node would be "{}" or have properties
    EXPECT_NE(result, "null");
}
