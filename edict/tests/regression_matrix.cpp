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
#include <vector>
#include <string>
#include <iostream>

using namespace agentc::edict;

// Helper to get opcode name (Internal)
std::string getOpName(int op) {
    switch(op) {
        case VMOP_RESET: return "RESET";
        case VMOP_YIELD: return "YIELD";
        case VMOP_PUSHEXT: return "PUSHEXT";
        case VMOP_DUP: return "DUP";
        case VMOP_SWAP: return "SWAP";
        case VMOP_POP: return "POP";
        case VMOP_REF: return "REF";
        case VMOP_ASSIGN: return "ASSIGN";
        case VMOP_REMOVE: return "REMOVE";
        case VMOP_EVAL: return "EVAL";
        case VMOP_CTX_PUSH: return "CTX_PUSH";
        case VMOP_CTX_POP: return "CTX_POP";
        case VMOP_FUN_PUSH: return "FUN_PUSH";
        case VMOP_FUN_EVAL: return "FUN_EVAL";
        case VMOP_FUN_POP: return "FUN_POP";
        case VMOP_FRAME_PUSH: return "FRAME_PUSH";
        case VMOP_FRAME_MERGE: return "FRAME_MERGE";
        case VMOP_THROW: return "THROW";
        case VMOP_CATCH: return "CATCH";
        case VMOP_S2S: return "S2S";
        case VMOP_D2S: return "D2S";
        case VMOP_E2S: return "E2S";
        case VMOP_F2S: return "F2S";
        case VMOP_S2D: return "S2D";
        case VMOP_S2E: return "S2E";
        case VMOP_S2F: return "S2F";
        case VMOP_CONCAT: return "CONCAT";
        case VMOP_LIST_ADD: return "LIST_ADD";
        case VMOP_FAIL: return "FAIL";
        case VMOP_TEST: return "TEST";
        case VMOP_PRINT: return "PRINT";
        default: return "UNKNOWN_" + std::to_string(op);
    }
}

// Helper to get analogous Edict Source Code
std::string getEdictSyntax(int op) {
    switch(op) {
        // Stack
        case VMOP_DUP: return "dup";
        case VMOP_SWAP: return "swap";
        case VMOP_POP: return "pop";
        
        // Dictionary
        case VMOP_REF: return "$";
        case VMOP_ASSIGN: return "@";
        case VMOP_REMOVE: return "/";
        
        // Context
        case VMOP_CTX_PUSH: return "{";
        case VMOP_CTX_POP: return "}";
        
        // Flow/Logic
        case VMOP_EVAL: return "!"; // or 'eval'
        case VMOP_THROW: return "&";
        case VMOP_CATCH: return "|";
        case VMOP_FAIL: return "fail";
        case VMOP_TEST: return "test";
        
        // Data
        case VMOP_PUSHEXT: return "123"; // Example literal
        case VMOP_PRINT: return "print";
        
        // Internal/System (No direct syntax usually, simulating via representation)
        case VMOP_LIST_ADD: return "]"; // End of list construction
        case VMOP_FUN_PUSH: return "(func...)";
        case VMOP_FRAME_PUSH: return "(frame-push)";
        
        default: return "(" + getOpName(op) + ")";
    }
}

class RegressionMatrixTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup common resources if needed
    }

    // Opcodes that require external I/O or invoke the cartographer/FFI subsystem.
    // These are inherently slow or side-effectful and are excluded from the matrix.
    static bool isHeavyOp(VMOpcode op) {
        switch (op) {
            case VMOP_MAP:                       // Parses a C++ header via cartographer
            case VMOP_LOAD:                      // Loads a shared library via FFI
            case VMOP_IMPORT:                    // Triggers async module import
            case VMOP_IMPORT_RESOLVED:
            case VMOP_IMPORT_DEFERRED:
            case VMOP_IMPORT_COLLECT:
            case VMOP_IMPORT_STATUS:
            case VMOP_PARSE_JSON:
            case VMOP_MATERIALIZE_JSON:
            case VMOP_RESOLVE_JSON:
            case VMOP_IMPORT_RESOLVED_JSON:
            case VMOP_READ_TEXT:                 // File I/O
            case VMOP_BOOTSTRAP_CURATE_PARSER:
            case VMOP_BOOTSTRAP_CURATE_RESOLVER:
            case VMOP_LOGIC_RUN:                 // Mini-Kanren search (potentially unbounded)
            case VMOP_SPECULATE:                 // Spawns a separate VM
                return true;
            default:
                return false;
        }
    }

    // Helper to run a single opcode with a prepared stack
    void RunOpcode(EdictVM& vm, VMOpcode op) {
        BytecodeBuffer code;
        code.addOp(op);
        if (op == VMOP_PUSHEXT) {
             code.addValue(Value("123")); // Dummy value
        }
        vm.execute(code);
    }
    
    // Prepare VM with a rich stack and take a checkpoint.
    // Callers should rollback to this checkpoint instead of calling reset(),
    // which would re-run loadBuiltins() + runStartupBootstrapPrelude() each time.
    EdictVM::TransactionCheckpoint PrepareVM(EdictVM& vm) {
        vm.reset();
        BytecodeBuffer code;
        code.addOp(VMOP_PUSHEXT); code.addValue(Value("1"));
        code.addOp(VMOP_PUSHEXT); code.addValue(Value("test"));
        vm.execute(code);
        return vm.beginTransaction();
    }
};

TEST_F(RegressionMatrixTest, StandaloneCoverage) {
    EdictVM vm;
    auto checkpoint = PrepareVM(vm);
    for (int i = 0; i < VMOP_COUNT; ++i) {
        VMOpcode op = static_cast<VMOpcode>(i);
        if (isHeavyOp(op)) continue;
        std::string syntax = getEdictSyntax(i);
        
        SCOPED_TRACE("Op: " + getOpName(i) + " // Edict: " + syntax);
        
        vm.rollbackTransaction(checkpoint);
        RunOpcode(vm, op);
        SUCCEED();
    }
}

TEST_F(RegressionMatrixTest, PairwiseCoverage) {
    // Construct the VM once and checkpoint its state after seeding the stack.
    // Rolling back to the checkpoint is O(1) vs reset() which re-runs
    // loadBuiltins() + runStartupBootstrapPrelude() on every iteration.
    // Heavy opcodes (FFI, cartographer, Kanren, speculate) are skipped — they
    // require external resources and are covered by their own dedicated test suites.
    EdictVM vm;
    auto checkpoint = PrepareVM(vm);
    for (int i = 0; i < VMOP_COUNT; ++i) {
        VMOpcode op1 = static_cast<VMOpcode>(i);
        if (isHeavyOp(op1)) continue;
        for (int j = 0; j < VMOP_COUNT; ++j) {
            VMOpcode op2 = static_cast<VMOpcode>(j);
            if (isHeavyOp(op2)) continue;
            
            std::string s1 = getEdictSyntax(i);
            std::string s2 = getEdictSyntax(j);
            
            SCOPED_TRACE("Pair: " + getOpName(i) + " -> " + getOpName(j) + " // Edict: " + s1 + " " + s2);
            
            vm.rollbackTransaction(checkpoint);
            RunOpcode(vm, op1);
            RunOpcode(vm, op2);
            
            SUCCEED();
        }
    }
}
