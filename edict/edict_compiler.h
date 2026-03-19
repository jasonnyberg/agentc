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

#include <string>
#include <vector>
#include <unordered_map>
#include "edict_types.h"

namespace agentc::edict {

// Token types
enum TokenType {
    TOKEN_LITERAL,
    TOKEN_IDENTIFIER,
    TOKEN_OPERATOR,
    TOKEN_CONTEXT_OPEN,
    TOKEN_CONTEXT_CLOSE,
    TOKEN_FUNCTION_OPEN,
    TOKEN_FUNCTION_CLOSE,
    TOKEN_STRING,
    TOKEN_COLON,
    TOKEN_COMMA,
    TOKEN_EOF
};

// Token structure
struct Token {
    TokenType type;
    std::string value;
    size_t position;  // byte offset in source
    bool hadSpaceBefore;
    uint32_t line = 1;  // 1-based line number
    uint32_t col  = 1;  // 1-based column number
};

// Tokenizer class
class Tokenizer {
public:
    Tokenizer(const std::string& input);
    Token nextToken();
    bool hasMoreTokens() const;
    void setJSONMode(bool mode) { jsonMode = mode; }
    
    // Current line/column (updated as tokens are consumed)
    uint32_t getLine() const { return line_; }
    uint32_t getCol()  const { return col_; }

private:
    std::string input;
    size_t position;
    bool jsonMode;
    uint32_t line_ = 1;  // 1-based line counter
    uint32_t col_  = 1;  // 1-based column counter
    
    bool skipWhitespace();
    Token parseLiteral(char openChar, bool hadSpaceBefore);
    Token parseString(bool hadSpaceBefore);
    Token parseIdentifier(bool hadSpaceBefore);
    Token parseOperator(bool hadSpaceBefore);
    Token parseContextOperator(bool hadSpaceBefore);

    // Stamp current source location into a token.
    Token stamp(Token t) const { t.line = line_; t.col = col_; return t; }
};

// Compiler class
class EdictCompiler {
public:
    EdictCompiler();
    
    // Compile source code to bytecode
    BytecodeBuffer compile(const std::string& source);
    
private:
    Tokenizer tokenizer;
    BytecodeBuffer output;
    Token currentToken;
    
    // Token handling
    void nextToken();
    bool match(TokenType type);
    bool match(const std::string& value);
    void expect(TokenType type);
    void expect(const std::string& value);
    
    // Compilation methods
    void compileTerm();
    void compileLiteral();
    void compileIdentifier();
    void compileLogicBlock();
    void compileSpeculateBlock();
    void compileOperator(const std::string& op);
    void compileContextOperator(char op);
    
    // JSON Compilation
    void compileJSONObject();
    void compileJSONArray();
    void compileJSONValue();
    
    // Bytecode emission
    void emitOperation(VMOpcode op);
    void emitLiteral(const std::string& literal);
    void emitValue(const Value& value);
    void emitList();
    void emitListStart();
    
    // Operator mapping
    std::unordered_map<std::string, VMOpcode> operatorMap;
};

} // namespace agentc::edict
