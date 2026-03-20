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

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <variant>
#include <unordered_map>
#include "../core/alloc.h"

namespace agentc::edict {

// VM operation codes
enum VMOpcode {
    // Core VM operations
    VMOP_RESET,
    VMOP_YIELD,
    VMOP_PUSHEXT,
    VMOP_SPLICE,
    
    // Stack operations
    VMOP_DUP,
    VMOP_SWAP,
    VMOP_POP,
    
    // Dictionary operations
    VMOP_REF,
    VMOP_ASSIGN,
    VMOP_REMOVE,
    
    // Evaluation operations
    VMOP_EVAL,
    
    // Context operations
    VMOP_CTX_PUSH,
    VMOP_CTX_POP,
    VMOP_FUN_PUSH,
    VMOP_FUN_EVAL,
    VMOP_FUN_POP,
    
    // Frame operations
    VMOP_FRAME_PUSH,
    VMOP_FRAME_MERGE,
    
    // Exception handling
    VMOP_THROW,
    VMOP_CATCH,
    
    // Stack transfer operations
    VMOP_S2S,  // Stack to stack (duplicate)
    VMOP_D2S,  // Dictionary to stack
    VMOP_E2S,  // Exception to stack
    VMOP_F2S,  // Function to stack
    VMOP_S2D,  // Stack to dictionary
    VMOP_S2E,  // Stack to exception
    VMOP_S2F,  // Stack to function
    
    // String and Structure operations
    VMOP_CONCAT,
    VMOP_LIST_ADD,    // Add value to list (node val -- node)
    
    // State Stack Ops
    VMOP_FAIL,
    VMOP_TEST,
    
    // FFI operations
    VMOP_MAP,
    VMOP_LOAD,
    VMOP_IMPORT,
    VMOP_IMPORT_RESOLVED,
    VMOP_IMPORT_DEFERRED,
    VMOP_IMPORT_COLLECT,
    VMOP_IMPORT_STATUS,
    VMOP_PARSE_JSON,
    VMOP_MATERIALIZE_JSON,
    VMOP_RESOLVE_JSON,
    VMOP_IMPORT_RESOLVED_JSON,
    VMOP_READ_TEXT,
    VMOP_REQUEST_ID,
    VMOP_BOOTSTRAP_CURATE_PARSER,
    VMOP_BOOTSTRAP_CURATE_RESOLVER,
    VMOP_CALL,
    VMOP_CLOSURE,
    VMOP_LOGIC_RUN,
    VMOP_REWRITE_DEFINE,
    VMOP_REWRITE_LIST,
    VMOP_REWRITE_REMOVE,
    VMOP_REWRITE_APPLY,
    VMOP_REWRITE_MODE,
    VMOP_REWRITE_TRACE,
    VMOP_SPECULATE,
    VMOP_UNSAFE_EXTENSIONS_ALLOW,
    VMOP_UNSAFE_EXTENSIONS_BLOCK,
    VMOP_UNSAFE_EXTENSIONS_STATUS,

    // Miscellaneous
    VMOP_PRINT,
    VMOP_HEAP_UTILIZATION,

    // Cursor navigation operations
    VMOP_CURSOR_DOWN,  // Move cursor to first child
    VMOP_CURSOR_UP,    // Move cursor to parent
    VMOP_CURSOR_NEXT,  // Move cursor to next sibling
    VMOP_CURSOR_PREV,  // Move cursor to previous sibling
    VMOP_CURSOR_GET,   // Push current cursor node value onto stack
    VMOP_CURSOR_SET,   // Pop stack top and set as current cursor node value
    
    // Number of opcodes (must be last)
    VMOP_COUNT
};

// VMOP_PUSHEXT inline payload type tags.
// These are the type bytes written after a VMOP_PUSHEXT opcode in the bytecode stream.
// They must match the switch in EdictVM::readValue() and the push_back() calls in
// EdictCompiler::emitList() / emitListStart().
enum VMPushextType : uint8_t {
    VMEXT_NULL       = 0, // null value
    VMEXT_BOOL       = 1, // boolean (1 byte follows: 0 or 1)
    VMEXT_STRING     = 2, // string (4-byte length + bytes)
    VMEXT_DICT       = 3, // serialized dictionary (4-byte length + JSON bytes)
    VMEXT_LIST       = 4, // empty list (no further bytes)
};

// VM state flags
enum VMState {
    VM_NORMAL    = 0x00,
    VM_YIELD     = 0x01,
    VM_ERROR     = 0x02,
    VM_COMPLETE  = 0x04,
    VM_SCANNING  = 0x08  // Skipping instructions
};

// VM resource indices
enum VMResource {
    VMRES_DICT,   // Stack of dictionary contexts
    VMRES_STACK,  // Stack of data stacks (frames)
    VMRES_FUNC,   // Stack of pending function thunks
    VMRES_EXCP,   // Stack of exceptions
    VMRES_CODE,   // Stack of code segments
    VMRES_STATE,  // Stack of conditions/errors
    VMRES_COUNT
};

// Value types
enum ValueType {
    VALUE_NULL,
    VALUE_BOOL,
    VALUE_STRING,
    VALUE_REFERENCE,
    VALUE_DICTIONARY
};

// Forward declarations
class Value;
class BytecodeBuffer;

// Dictionary class for storing named values
class Dictionary {
private:
    std::unordered_map<std::string, std::shared_ptr<Value>> entries;

public:
    Dictionary() = default;
    
    // Core dictionary operations
    void set(const std::string& key, const Value& value);
    Value get(const std::string& key) const;
    bool remove(const std::string& key);
    bool contains(const std::string& key) const;
    
    // Serialization support
    std::string serialize() const;
    static Dictionary deserialize(const std::string& data);
    
    // Iterator access
    auto begin() { return entries.begin(); }
    auto end() { return entries.end(); }
    auto begin() const { return entries.begin(); }
    auto end() const { return entries.end(); }
    
    // Size
    size_t size() const { return entries.size(); }
    bool empty() const { return entries.empty(); }
};

// Value class for storing different types of values
class Value {
public:
    // Constructors
    Value() : type(ValueType::VALUE_NULL) {}
    Value(std::nullptr_t) : type(ValueType::VALUE_NULL) {}
    Value(bool b) : type(ValueType::VALUE_BOOL), boolValue(b) {}
    Value(const std::string& s) : type(ValueType::VALUE_STRING), stringValue(s) {}
    Value(const char* s) : type(ValueType::VALUE_STRING), stringValue(s) {}
    Value(const Dictionary& dict);
    
    template<typename T>
    Value(CPtr<T> ptr) : type(ValueType::VALUE_REFERENCE), referenceValue(std::make_shared<CPtr<T>>(ptr)) {}
    
    // Type checking
    bool isNull() const { return type == ValueType::VALUE_NULL; }
    bool isBool() const { return type == ValueType::VALUE_BOOL; }
    bool isString() const { return type == ValueType::VALUE_STRING; }
    bool isReference() const { return type == ValueType::VALUE_REFERENCE; }
    bool isDictionary() const { return type == ValueType::VALUE_DICTIONARY; }
    
    // Value retrieval
    bool asBool() const { 
        if (!isBool()) throw std::runtime_error("Value is not a boolean");
        return boolValue; 
    }
    
    const std::string& asString() const { 
        if (!isString()) throw std::runtime_error("Value is not a string");
        return stringValue; 
    }
    
    template<typename T>
    CPtr<T> asReference() const { 
        if (!isReference()) throw std::runtime_error("Value is not a reference");
        auto ptr = std::static_pointer_cast<CPtr<T>>(referenceValue);
        return *ptr;
    }
    
    Dictionary& asDictionary();
    const Dictionary& asDictionary() const;
    
    // String representation
    std::string toString() const;
    
private:
    ValueType type;
    
    union {
        bool boolValue;
    };
    
    // These can't be in the union because they have non-trivial constructors/destructors
    std::string stringValue;
    std::shared_ptr<void> referenceValue;
    std::shared_ptr<Dictionary> dictionaryValue;
};

// Bytecode buffer class
class BytecodeBuffer {
public:
    BytecodeBuffer() = default;
    
    // Add operations to the buffer
    void addOp(VMOpcode op);
    void addString(const std::string& value);
    void addValue(const Value& value);
    void clear();
    
    // Access the buffer
    std::vector<uint8_t>& getData() { return data; }
    const std::vector<uint8_t>& getData() const { return data; }
    
private:
    std::vector<uint8_t> data;
};

} // namespace agentc::edict
