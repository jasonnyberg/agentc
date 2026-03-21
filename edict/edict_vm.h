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

#include <vector>
#include <string>
#include <functional>
#include <optional>
#include "../core/cursor.h"
#include "../kanren/kanren.h"
#include "edict_types.h"

namespace agentc::edict {

// Forward declarations
class EdictCompiler;

} // namespace agentc::edict

namespace agentc { namespace cartographer { class Mapper; class FFI; class CartographerService; class Boxing; } }

namespace agentc::edict {

// Virtual Machine class
class EdictVM {
public:
    enum class RewriteMode {
        Auto,
        Manual,
        Off,
    };

    struct TransactionCheckpoint {
        bool valid = false;
        CPtr<agentc::ListreeValue> resources[VMRES_COUNT];
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> rewriteRules;
        Allocator<agentc::ListreeValue>::Checkpoint listreeValue;
        Allocator<agentc::ListreeItem>::Checkpoint listreeItem;
        Allocator<agentc::ListreeValueRef>::Checkpoint listreeValueRef;
        Allocator<CLL<agentc::ListreeValueRef>>::Checkpoint listNode;
        Allocator<AATree<agentc::ListreeItem>>::Checkpoint treeNode;
        BlobAllocator::Checkpoint blobAllocator;
        uint32_t savedState = VM_NORMAL;
        size_t savedInstructionPtr = 0;
        CPtr<agentc::ListreeValue> savedExceptionValue;
        const uint8_t* savedCodePtr = nullptr;
        size_t savedCodeSize = 0;
        bool savedTailEval = false;
        int savedScanDepth = 0;
    };

    EdictVM(CPtr<agentc::ListreeValue> root = nullptr);
    ~EdictVM(); // Required for unique_ptr with forward decl
    
    // Execute bytecode
    int execute(const BytecodeBuffer& code);
    
    // Error handling
    void setError(const std::string& message) { 
        error_message = message; 
        state |= VM_ERROR;
    }
    
    std::string getError() const { return error_message; }
    
    // VM state management
    void reset() { 
        initResources(cursor.getValue());
        state = VM_NORMAL;
        error_message.clear();
        exception_value = nullptr;
    }
    
    // Cursor access
    agentc::Cursor& getCursor() { return cursor; }
    void setCursor(CPtr<agentc::ListreeValue> root) { cursor = agentc::Cursor(root); }
    
    // Register cursor operations
    void registerCursorOperations();
    void loadBuiltins();
    void loadCoreBuiltins();
    void installBootstrapImportCapsule();

    // Rewrite rule registration (minimal prototype)
    void addRewriteRule(const std::vector<std::string>& pattern, const std::vector<std::string>& replacement);
    size_t getRewriteRuleCount() const;
    std::vector<std::string> getRewriteRulePattern(size_t index) const;
    std::vector<std::string> getRewriteRuleReplacement(size_t index) const;
    bool removeRewriteRule(size_t index);
    RewriteMode getRewriteMode() const { return rewrite_mode; }
    void setRewriteMode(RewriteMode mode) { rewrite_mode = mode; }
    CPtr<agentc::ListreeValue> getLastRewriteTrace() const { return last_rewrite_trace; }
    bool getAllowUnsafeFfiCalls() const { return allow_unsafe_ffi_calls; }
    void setAllowUnsafeFfiCalls(bool allow) { allow_unsafe_ffi_calls = allow; }

    // Transaction helpers for speculative cognitive execution.
    TransactionCheckpoint beginTransaction();
    bool rollbackTransaction(TransactionCheckpoint& checkpoint);
    bool commitTransaction(TransactionCheckpoint& checkpoint);
    bool speculate(const std::function<bool(EdictVM&)>& action,
                   CPtr<agentc::ListreeValue>& resultOut,
                   std::string& errorOut);
    bool speculateValue(const std::function<CPtr<agentc::ListreeValue>(EdictVM&)>& action,
                        CPtr<agentc::ListreeValue>& resultOut,
                        std::string& errorOut);
    bool speculate(const BytecodeBuffer& code,
                   CPtr<agentc::ListreeValue>& resultOut,
                   std::string& errorOut);
    
    // Stack helpers
    CPtr<agentc::ListreeValue> getStackTop() const;
    size_t getStackSize() const;
    size_t getResourceDepth(VMResource res) const;
    CPtr<agentc::ListreeValue> dumpStack() const;
    
    // Data stack convenience
    void pushData(CPtr<agentc::ListreeValue> v) { stack_enq(VMRES_STACK, v); }
    CPtr<agentc::ListreeValue> popData() { return stack_deq(VMRES_STACK, true); }
    CPtr<agentc::ListreeValue> peekData() const { return const_cast<EdictVM*>(this)->stack_deq(VMRES_STACK, false); }
    
    // Services
    std::unique_ptr<agentc::cartographer::Mapper> mapper;
    std::unique_ptr<agentc::cartographer::FFI> ffi;
    std::unique_ptr<agentc::cartographer::CartographerService> cartographer;

private:
    enum class ScanMode {
        None,
        FromThrow,
        FromCatch,
    };

    agentc::Cursor cursor;
    // Resources are now a stack of lists (Listree)
    CPtr<agentc::ListreeValue> resources[VMRES_COUNT];
    struct RewriteRule {
        std::vector<std::string> pattern;
        std::vector<std::string> replacement;
    };
    std::vector<RewriteRule> rewrite_rules;
    
    uint32_t state;
    size_t instruction_ptr;
    std::string error_message;
    CPtr<agentc::ListreeValue> exception_value;
    const uint8_t* code_ptr;
    size_t code_size;
    bool tail_eval;
    ScanMode scan_mode = ScanMode::None;
    int scan_depth = 0;
    
    // Exception handling helper
    bool handleException();

    // Low-level resource management (enq/deq to the resource stack itself)
    void enq(VMResource res, CPtr<agentc::ListreeValue> v);
    CPtr<agentc::ListreeValue> deq(VMResource res, bool pop = true);
    CPtr<agentc::ListreeValue> peek(VMResource res) const;

    // High-level stack management (enq/deq to the ACTIVE FRAME in the resource)
    void stack_enq(VMResource res, CPtr<agentc::ListreeValue> v);
    CPtr<agentc::ListreeValue> stack_deq(VMResource res, bool pop = true);

    uint8_t readByte();
    int readInt();
    std::string readString();
    CPtr<agentc::ListreeValue> readValue();
    
    // Operation handlers
    void op_RESET();
    void op_EXT();
    void op_PUSHEXT();
    void op_SPLICE();
    void op_DUP();
    void op_SWAP();
    void op_POP();
    void op_REF();
    void op_DEREF();
    void op_ASSIGN();
    void op_REMOVE();
    void op_EVAL();
    void op_CTX_PUSH();
    void op_CTX_POP();
    void op_FUN_PUSH();
    void op_FUN_EVAL();
    void op_FUN_POP();
    void op_FRAME_PUSH();
    void op_FRAME_MERGE();
    void op_THROW();
    void op_CATCH();
    void op_S2S();
    void op_D2S();
    void op_E2S();
    void op_F2S();
    void op_S2D();
    void op_S2E();
    void op_S2F();
    void op_CONCAT();
    void op_LIST_ADD();
    void op_PRINT();
    void op_FAIL();
    void op_TEST();
    void op_YIELD();
    
    // FFI Ops
    void op_MAP();
    void op_LOAD();
    void op_IMPORT();
    void op_IMPORT_RESOLVED();
    void op_IMPORT_DEFERRED();
    void op_IMPORT_COLLECT();
    void op_IMPORT_STATUS();
    void op_PARSE_JSON();
    void op_MATERIALIZE_JSON();
    void op_RESOLVE_JSON();
    void op_IMPORT_RESOLVED_JSON();
    void op_READ_TEXT();
    void op_REQUEST_ID();
    void op_BOOTSTRAP_CURATE_PARSER();
    void op_BOOTSTRAP_CURATE_RESOLVER();
    void op_BOOTSTRAP_CURATE_CARTOGRAPHER();
    void op_BOX();
    void op_UNBOX();
    void op_BOX_FREE();
    void op_CALL(); // Placeholder/Unused if integrating into EVAL
    void op_CLOSURE();
    void op_LOGIC_RUN();
    void op_REWRITE_DEFINE();
    void op_REWRITE_LIST();
    void op_REWRITE_REMOVE();
    void op_REWRITE_APPLY();
    void op_REWRITE_MODE();
    void op_REWRITE_TRACE();
    void op_SPECULATE();
    void op_UNSAFE_EXTENSIONS_ALLOW();
    void op_UNSAFE_EXTENSIONS_BLOCK();
    void op_UNSAFE_EXTENSIONS_STATUS();
    void op_HEAP_UTILIZATION();

    // Cursor navigation ops (registered via registerCursorOperations)
    void op_CURSOR_DOWN();  // Move cursor to first child; push bool result
    void op_CURSOR_UP();    // Move cursor to parent; push bool result
    void op_CURSOR_NEXT();  // Move cursor to next sibling; push bool result
    void op_CURSOR_PREV();  // Move cursor to previous sibling; push bool result
    void op_CURSOR_GET();   // Push current cursor node onto data stack
    void op_CURSOR_SET();   // Pop data stack; set as current cursor node value

    // op_EVAL phase helpers — each returns true if it handled the value.
    // This decomposition makes each phase independently testable and lets
    // op_EVAL() read as a clean dispatch sequence.
    bool evalDispatchIterator(CPtr<agentc::ListreeValue> v);
    bool evalDispatchThunk(CPtr<agentc::ListreeValue> v);
    bool evalDispatchFFI(CPtr<agentc::ListreeValue> v);
    void evalDispatchSource(CPtr<agentc::ListreeValue> v);

    // Listree concatenation helper
    void listcat(VMResource res);

    // Helper for cursor iteration in FFI
    void executeIterativeFFI(const std::string& funcName, CPtr<agentc::ListreeValue> funcDef, 
                             CPtr<agentc::ListreeValue> args,
                             size_t index, 
                             CPtr<agentc::ListreeValue> builtArgs,
                             CPtr<agentc::ListreeValue>& resultList);

    CPtr<agentc::ListreeValue> buildClosureValue(CPtr<agentc::ListreeValue> signature,
                                             CPtr<agentc::ListreeValue> agentFunction);

    // Rewrite helpers
    void initResources(CPtr<agentc::ListreeValue> root);
    void resetRuntime();
    bool applyRewriteOnce(bool manualTrigger = false);
    void applyRewriteLoop(bool manualTrigger = false);
    bool enforceImportedFunctionPolicy(const std::string& funcName, CPtr<agentc::ListreeValue> funcDef);
    void addBuiltinThunk(CPtr<agentc::ListreeValue> dictVal, const std::string& name, uint8_t opcode);
    void addCompiledThunk(CPtr<agentc::ListreeValue> dictVal, const std::string& name, const std::string& source);
    CPtr<agentc::ListreeValue> createBootstrapCuratedParser();
    CPtr<agentc::ListreeValue> createBootstrapCuratedResolver();
    CPtr<agentc::ListreeValue> createBootstrapCuratedCartographer();
    void runStartupBootstrapPrelude();

    RewriteMode rewrite_mode = RewriteMode::Auto;
    CPtr<agentc::ListreeValue> last_rewrite_trace;
    bool allow_unsafe_ffi_calls = false;

    // Code frame helpers
    CPtr<agentc::ListreeValue> makeCodeFrame(const BytecodeBuffer& code);
    void writeFrameIp(CPtr<agentc::ListreeValue> frame, int ip);
    int readFrameIp(CPtr<agentc::ListreeValue> frame) const;
    void pushCodeFrame(const BytecodeBuffer& code);
    void popCodeFrame();
    CPtr<agentc::ListreeValue> peekCodeFrame();
};

} // namespace agentc::edict
