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

#include <gtest/gtest.h>
#include "../edict_vm.h"
#include "../edict_types.h"
#include "../edict_types.h"
#include "../edict_compiler.h"

using namespace agentc::edict;

static std::string valueToString(const CPtr<agentc::ListreeValue>& v) {
    if (!v || !v->getData() || v->getLength() == 0) return "";
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return "";
    return std::string(static_cast<char*>(v->getData()), v->getLength());
}

static bool isNullValue(const CPtr<agentc::ListreeValue>& v) {
    return v && (v->getFlags() & agentc::LtvFlags::Null) != agentc::LtvFlags::None;
}

static int runCode(EdictVM& vm, const std::string& source) {
    EdictCompiler compiler;
    BytecodeBuffer code = compiler.compile(source);
    return vm.execute(code);
}

TEST(EdictVM, EvalTest) {
    EdictVM vm;
    BytecodeBuffer code;
    code.addOp(VMOP_RESET);
    code.addOp(VMOP_PUSHEXT);
    code.addValue(Value("1 2"));
    code.addOp(VMOP_EVAL);
    int res = vm.execute(code);
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getStackSize(), 2);
}

TEST(EdictVM, AutoDerefAssignment) {
    EdictVM vm;
    int res = runCode(vm, "[hello]@x x");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "hello");
}

TEST(EdictVM, YieldedExecutionCanResumeCurrentCodeFrame) {
    EdictVM vm;
    int res = runCode(vm, "'before yield! 'after");
    ASSERT_TRUE(res & VM_YIELD);
    EXPECT_FALSE(res & VM_COMPLETE);

    auto yieldedPhase = vm.getStackTop();
    EXPECT_EQ(valueToString(yieldedPhase), "before");

    res = vm.resume();
    EXPECT_FALSE(res & VM_YIELD);
    EXPECT_TRUE(res & VM_COMPLETE);
    EXPECT_EQ(valueToString(vm.getStackTop()), "after");
}

TEST(EdictVM, FrozenThunkUsesPrivateInstructionPointerAcrossResumeAndReuse) {
    EdictVM vm;
    EdictCompiler compiler;

    int res = vm.execute(compiler.compile("[ 'before yield! 'after ] freeze! @t t!"));
    ASSERT_TRUE(res & VM_YIELD) << vm.getError();
    EXPECT_EQ(valueToString(vm.getStackTop()), "before");

    res = vm.resume();
    ASSERT_FALSE(res & VM_YIELD) << vm.getError();
    ASSERT_TRUE(res & VM_COMPLETE) << vm.getError();
    EXPECT_EQ(valueToString(vm.getStackTop()), "after");

    res = vm.execute(compiler.compile("t!"));
    ASSERT_TRUE(res & VM_YIELD) << vm.getError();
    EXPECT_EQ(valueToString(vm.getStackTop()), "before");

    res = vm.resume();
    ASSERT_FALSE(res & VM_YIELD) << vm.getError();
    ASSERT_TRUE(res & VM_COMPLETE) << vm.getError();
    EXPECT_EQ(valueToString(vm.getStackTop()), "after");
}

TEST(EdictVM, UndefinedSymbolFallsBack) {
    EdictVM vm;
    int res = runCode(vm, "not_defined");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "not_defined");
}

TEST(EdictVM, StrictLookupReturnsNullForUndefinedSymbol) {
    EdictVM vm;
    int res = runCode(vm, "strict! not_defined");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getResourceDepth(VMRES_STATE), 0u);
    EXPECT_TRUE(isNullValue(vm.getStackTop()));
}

TEST(EdictVM, StrictLookupReturnsNullForUndefinedDottedPath) {
    EdictVM vm;
    int res = runCode(vm, R"({"nested":{"value":"ok"}} @root strict! root.nested.missing)");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getResourceDepth(VMRES_STATE), 0u);
    EXPECT_TRUE(isNullValue(vm.getStackTop()));
}

TEST(EdictVM, StrictFailLookupPushesNullAndFailureState) {
    EdictVM vm;
    int res = runCode(vm, "strict_fail! not_defined");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getResourceDepth(VMRES_STATE), 1u);
    EXPECT_TRUE(isNullValue(vm.getStackTop()));
}

TEST(EdictVM, LaxLookupStillFallsBackAfterStrictMode) {
    EdictVM vm;
    int res = runCode(vm, "strict! not_defined / lax! missing_again");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getResourceDepth(VMRES_STATE), 0u);
    EXPECT_EQ(valueToString(vm.getStackTop()), "missing_again");
}

TEST(EdictVM, RemoveSymbol) {
    EdictVM vm;
    int res = runCode(vm, "[hello]@x /x x");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "x");
}

TEST(EdictVM, RemoveTopOfStack) {
    EdictVM vm;
    int res = runCode(vm, "1 2 3 /");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getStackSize(), 2);
    auto top = vm.getStackTop();
    EXPECT_TRUE(bool(top));
}

TEST(EdictVM, PopIsNotAStackDiscardKeyword) {
    EdictVM vm;
    int res = runCode(vm, "'sentinel pop");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(vm.getStackSize(), 2);
    EXPECT_EQ(valueToString(vm.getStackTop()), "pop");
}

TEST(EdictVM, ConcatenatedPrefixSigilsApplyToSameIdentifierInOrder) {
    EdictVM vm;
    int res = runCode(vm, "[x]@a [y]/@a a");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(valueToString(vm.getStackTop()), "y");
}

TEST(EdictVM, ConcatenatedRemoveAssignDoesNotPopReplacementValue) {
    EdictVM vm;
    int res = runCode(vm, "[old]@a [new]/@a a");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(valueToString(vm.getStackTop()), "new");
}

TEST(EdictVM, ConcatenatedMultipleRemovesApplyToSameIdentifier) {
    EdictVM vm;
    int res = runCode(vm, "[x]@a [y]@a [z]@a //a a");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(valueToString(vm.getStackTop()), "x");
}

TEST(EdictVM, TailPrefixedLookupReadsDictionaryValueTail) {
    EdictVM vm;
    int res = runCode(vm, "[x]@a [y]@a [z]@a -a");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(valueToString(vm.getStackTop()), "x");
}

TEST(EdictVM, TailPrefixedAssignmentAddsDictionaryValueTail) {
    EdictVM vm;
    int res = runCode(vm, "[x]@a [y]@a [z]@a [t]@-a -a");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(valueToString(vm.getStackTop()), "t");
}

TEST(EdictVM, TailPrefixedRemoveDiscardsDictionaryValueTail) {
    EdictVM vm;
    int res = runCode(vm, "[x]@a [y]@a [z]@a /-a -a");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(valueToString(vm.getStackTop()), "y");
}

TEST(EdictVM, TailPrefixedRemoveAssignReplacesDictionaryValueTail) {
    EdictVM vm;
    int res = runCode(vm, "[x]@a [y]@a [z]@a [t]/@-a -a");
    EXPECT_FALSE(res & 0x02);
    EXPECT_EQ(valueToString(vm.getStackTop()), "t");
}

TEST(EdictVM, TailPrefixedRemoveCleansUpWhenHistoryBecomesEmpty) {
    EdictVM vm;
    int res = runCode(vm, "[x]@a [y]@a [z]@a ///-a strict! a");
    EXPECT_FALSE(res & 0x02);
    EXPECT_TRUE(isNullValue(vm.getStackTop()));
}

TEST(EdictVM, SpliceNameSyntaxStillWorksAfterPrefixChainParsing) {
    EdictVM vm;
    int res = runCode(vm, "[] @collector [^collector^] @capture capture([one] [two]) collector to_json!");
    EXPECT_FALSE(res & 0x02);
    EXPECT_NE(valueToString(vm.getStackTop()).find("one"), std::string::npos);
    EXPECT_NE(valueToString(vm.getStackTop()).find("two"), std::string::npos);
}

TEST(EdictVM, DeferredEvalWithMethodCall) {
    EdictVM vm;
    int res = runCode(vm, "[!]@myeval [dup]@twice myeval([hello] twice)");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "hello");

    BytecodeBuffer popCode;
    popCode.addOp(VMOP_POP);
    res = vm.execute(popCode);
    EXPECT_FALSE(res & 0x02);
    top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "hello");
}

TEST(EdictVM, NestedEvalUsesCodeStack) {
    EdictVM vm;
    int res = runCode(vm, "[[hello]!]!");
    EXPECT_FALSE(res & 0x02);
    auto top = vm.getStackTop();
    EXPECT_EQ(valueToString(top), "hello");
}

// ======================================================================
// G058 — freeze builtin tests
// ======================================================================

TEST(FreezeBuiltin, FreezeMakesValueReadOnly) {
    EdictVM vm;
    EdictCompiler compiler;
    // Push a dict, then freeze it.  The frozen value is still on the stack.
    int state = vm.execute(compiler.compile("{\"a\": \"1\"} freeze!"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto top = vm.getStackTop();
    ASSERT_TRUE(bool(top));
    EXPECT_TRUE(top->isReadOnly());
}

TEST(FreezeBuiltin, FrozenValueCanStillBeRead) {
    EdictVM vm;
    EdictCompiler compiler;
    int state = vm.execute(compiler.compile("{\"a\": \"hello\"} freeze! @frozen / frozen"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto top = vm.getStackTop();
    ASSERT_TRUE(bool(top));
    EXPECT_TRUE(top->isReadOnly());
    auto item = top->find("a");
    ASSERT_TRUE(bool(item));
    auto val = item->getValue(false, false);
    ASSERT_TRUE(bool(val));
    EXPECT_EQ(std::string(static_cast<char*>(val->getData()), val->getLength()), "hello");
}

TEST(FreezeBuiltin, FreezeIsIdempotentOnAlreadyFrozen) {
    EdictVM vm;
    EdictCompiler compiler;
    // Double-freeze should not crash or produce errors.
    int state = vm.execute(compiler.compile("{} freeze! freeze! @x / x"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto top = vm.getStackTop();
    ASSERT_TRUE(bool(top));
    EXPECT_TRUE(top->isReadOnly());
}

TEST(FreezeBuiltin, CopyOfFrozenValueReturnsSameSlab) {
    EdictVM vm;
    EdictCompiler compiler;
    // After freezing, the value is on the stack.  Pop it, check that
    // copy() returns the same node (O(1) share path).
    int state = vm.execute(compiler.compile("{\"k\": \"v\"} freeze!"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto frozen = vm.popData();
    ASSERT_TRUE(bool(frozen));
    ASSERT_TRUE(frozen->isReadOnly());

    SlabId original = frozen.getSlabId();
    CPtr<agentc::ListreeValue> copied = frozen->copy();
    EXPECT_EQ(copied.getSlabId(), original);
}

TEST(FreezeBuiltin, PathRemoveCannotMutateFrozenObject) {
    EdictVM vm;
    EdictCompiler compiler;
    int state = vm.execute(compiler.compile("{\"a\":\"1\"} freeze! @f / f.a /f.a f to_json!"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto top = vm.getStackTop();
    ASSERT_TRUE(bool(top));
    EXPECT_EQ(valueToString(top), "{\"a\":\"1\"}");
}

TEST(FreezeBuiltin, PrefixRemoveHeadCannotMutateFrozenObjectHistory) {
    EdictVM vm;
    EdictCompiler compiler;
    int state = vm.execute(compiler.compile("{\"a\":\"1\"} freeze! @f / f.a //f.a f to_json!"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto top = vm.getStackTop();
    ASSERT_TRUE(bool(top));
    EXPECT_EQ(valueToString(top), "{\"a\":\"1\"}");
}

TEST(SharingTest, CrossVMSharedReadOnlyBranch) {
    auto shared = agentc::createListValue();
    shared->put(agentc::createStringValue("data"));
    shared->setReadOnly(true);
    
    agentc::edict::EdictVM vm1(shared);
    agentc::edict::EdictVM vm2(shared);
    
    auto val1 = vm1.getCursor().getValue()->get(false, false);
    auto val2 = vm2.getCursor().getValue()->get(false, false);
    
    std::string s1((char*)val1->getData(), val1->getLength());
    std::string s2((char*)val2->getData(), val2->getLength());
    
    EXPECT_EQ(s1, "data");
    EXPECT_EQ(s2, "data");
}
