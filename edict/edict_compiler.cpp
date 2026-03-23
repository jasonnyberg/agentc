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

#include "edict_compiler.h"
#include <sstream>
#include <cstring>
#include <iostream>
#include <cstdlib>

namespace agentc::edict {

static bool edictTraceEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("EDICT_TRACE");
        return value && std::string(value) == "1";
    }();
    return enabled;
}

// Simple bytecode compiler implementation
BytecodeBuffer EdictCompiler::compile(const std::string& source) {
    // std::cerr << "Compiling: " << source << std::endl;
    BytecodeBuffer buffer;
    
    // Initialize tokenizer with source
    tokenizer = Tokenizer(source);
    output = BytecodeBuffer();
    
    // Get first token
    nextToken();
    
    // Compile each term in the source
    while (currentToken.type != TOKEN_EOF) {
        compileTerm();
    }
    
    return output;
}

// Tokenizer implementation
Tokenizer::Tokenizer(const std::string& input) : input(input), position(0), jsonMode(false), line_(1), col_(1) {
    // Skip initial whitespace (updates line_/col_)
    skipWhitespace();
}

bool Tokenizer::skipWhitespace() {
    bool skipped = false;
    while (position < input.length() && std::isspace(input[position])) {
        if (input[position] == '\n') {
            ++line_;
            col_ = 1;
        } else {
            ++col_;
        }
        position++;
        skipped = true;
    }
    return skipped;
}

Token Tokenizer::nextToken() {
    bool skipped = skipWhitespace();
    // Capture source location at the start of this token (after whitespace).
    uint32_t tokLine = line_;
    uint32_t tokCol  = col_;
    
    if (position >= input.length()) {
        Token t{TOKEN_EOF, "", position, skipped};
        t.line = tokLine; t.col = tokCol;
        return t;
    }
    
    Token tok;
    // Check for literals (enclosed in square brackets)
    if (input[position] == '[' && !jsonMode) {
        tok = parseLiteral(input[position], skipped);
    }
    // Check for strings (used for JSON dict keys/values)
    else if (jsonMode && input[position] == '"') {
        tok = parseString(skipped);
    }
    // Check for quote-word form: 'word  (whitespace-terminated atom literal)
    else if (input[position] == '\'') {
        tok = parseQuoteWord(skipped);
    }
    // Check for context operators
    else if (input[position] == '{' || input[position] == '}' || input[position] == '<' || input[position] == '>' || 
        input[position] == '(' || input[position] == ')' ||
       (jsonMode && (input[position] == '[' || input[position] == ']'))) {
        tok = parseContextOperator(skipped);
    }
    // Check for operators
    else if (input[position] == '@' || input[position] == '!' || input[position] == '/' || input[position] == '^' ||
        input[position] == ':' || input[position] == ',' || input[position] == '&' || input[position] == '|') {
        tok = parseOperator(skipped);
    }
    // Check for identifiers (digits allowed, * allowed for cursor prefix, utf-8 allowed)
    else if (std::isalnum(input[position]) || input[position] == '_' || input[position] == '.' || input[position] == '-' || input[position] == '$' || input[position] == '*' || input[position] == '"' || (unsigned char)input[position] >= 0x80) {
        tok = parseIdentifier(skipped);
    }
    else {
        // Unknown token - Skip it but don't loop infinitely if skip fails
        tok = {TOKEN_EOF, std::string(1, input[position]), position, skipped};
        ++col_;
        position++;
    }
    tok.line = tokLine;
    tok.col  = tokCol;
    return tok;
}

Token Tokenizer::parseLiteral(char openChar, bool hadSpaceBefore) {
    size_t start = position;
    char closeChar = (openChar == '[') ? ']' : ')';
    ++col_; position++; // Skip opening delimiter
    
    int depth = 1;
    while (position < input.length() && depth > 0) {
        if (input[position] == '\n') { ++line_; col_ = 1; }
        else                         { ++col_; }
        if (input[position] == openChar) {
            depth++;
        } else if (input[position] == closeChar) {
            depth--;
        }
        position++;
    }
    
    if (depth > 0) {
        // Unclosed literal
        Token t{TOKEN_EOF, "", position, hadSpaceBefore};
        return t;
    }
    
    return {TOKEN_LITERAL, input.substr(start, position - start), start, hadSpaceBefore};
}

Token Tokenizer::parseString(bool hadSpaceBefore) {
    size_t start = position;
    ++col_; position++; // Skip opening "
    
    while (position < input.length() && input[position] != '"') {
        if (input[position] == '\\') {
            ++col_; position++; // Skip backslash
        }
        if (input[position] == '\n') { ++line_; col_ = 1; }
        else                         { ++col_; }
        position++;
    }
    
    if (position < input.length()) {
        ++col_; position++; // Skip closing "
    }
    
    return {TOKEN_STRING, input.substr(start, position - start), start, hadSpaceBefore};
}

Token Tokenizer::parseQuoteWord(bool hadSpaceBefore) {
    size_t start = position;
    ++col_; position++; // skip the leading '
    size_t wordStart = position;
    while (position < input.length() && !std::isspace(input[position])) {
        ++col_;
        position++;
    }
    return {TOKEN_QUOTE, input.substr(wordStart, position - wordStart), start, hadSpaceBefore};
}

    Token Tokenizer::parseIdentifier(bool hadSpaceBefore) {
    size_t start = position;
    
    // Regular identifier or number
    while (position < input.length() && (std::isalnum(input[position]) || input[position] == '_' || input[position] == '.' || input[position] == '-' || input[position] == '$' || input[position] == '*' || input[position] == '"' || (unsigned char)input[position] >= 0x80)) {
        ++col_;
        position++;
    }

    
    return {TOKEN_IDENTIFIER, input.substr(start, position - start), start, hadSpaceBefore};
}

Token Tokenizer::parseOperator(bool hadSpaceBefore) {
    size_t start = position;
    ++col_; position++;
    return {TOKEN_OPERATOR, input.substr(start, 1), start, hadSpaceBefore};
}

Token Tokenizer::parseContextOperator(bool hadSpaceBefore) {
    size_t start = position;
    char op = input[position];
    ++col_; position++;
    
    if (op == '{' || op == '<' || op == '[' || op == '(') {
        return {TOKEN_CONTEXT_OPEN, std::string(1, op), start, hadSpaceBefore};
    } else {
        return {TOKEN_CONTEXT_CLOSE, std::string(1, op), start, hadSpaceBefore};
    }
}

bool Tokenizer::hasMoreTokens() const {
    return position < input.length();
}

// EdictCompiler implementation
EdictCompiler::EdictCompiler() : tokenizer(""), output() {
    // Initialize operator map
    operatorMap["@"] = VMOP_ASSIGN;
    operatorMap["!"] = VMOP_EVAL;
    operatorMap["/"] = VMOP_REMOVE;
    operatorMap["&"] = VMOP_THROW;
    operatorMap["|"] = VMOP_CATCH;
}

void EdictCompiler::nextToken() {
    currentToken = tokenizer.nextToken();
}

bool EdictCompiler::match(TokenType type) {
    return currentToken.type == type;
}

bool EdictCompiler::match(const std::string& value) {
    return currentToken.value == value;
}

void EdictCompiler::expect(TokenType type) {
    if (!match(type)) {
        throw std::runtime_error(
            "line " + std::to_string(currentToken.line) +
            ":" + std::to_string(currentToken.col) +
            ": unexpected token type");
    }
    nextToken();
}

void EdictCompiler::expect(const std::string& value) {
    if (!match(value)) {
        std::cerr << "line " << currentToken.line << ":" << currentToken.col
                  << ": expected '" << value << "', got '"
                  << currentToken.value << "' (type " << currentToken.type << ")" << std::endl;
        throw std::runtime_error(
            "line " + std::to_string(currentToken.line) +
            ":" + std::to_string(currentToken.col) +
            ": unexpected token '" + currentToken.value + "'");
    }
    nextToken();
}

void EdictCompiler::compileTerm() {
    if (edictTraceEnabled()) {
        std::cout << "TOKEN: " << currentToken.value << " type=" << (int)currentToken.type << " space=" << currentToken.hadSpaceBefore << std::endl;
    }
    if (match(TOKEN_LITERAL)) {
        compileLiteral();
    } else if (match(TOKEN_QUOTE)) {
        // 'word — single-quote prefix literal: push bare string, no dictionary lookup
        emitValue(Value(currentToken.value));
        nextToken();
    } else if (match(TOKEN_IDENTIFIER)) {
        compileIdentifier();
    } else if (match(TOKEN_OPERATOR)) {
        std::string op = currentToken.value;
        nextToken();
        
        // Check if identifier is immediately following (no space)
        if ((op == "@" || op == "/" || op == "^") && 
            match(TOKEN_IDENTIFIER) && !currentToken.hadSpaceBefore) {
            emitValue(Value(currentToken.value));
            if (op == "^") {
                // Just push the value (already done by emitValue)
            } else if (op == "@") {
                // @name emits key after value, matching VMOP_ASSIGN's stack order.
                emitOperation(VMOP_ASSIGN);
            } else {
                compileOperator(op);
            }
            nextToken();
        } else if (op == "/") {
            emitOperation(VMOP_POP);
        } else if (op == "^") {
            emitOperation(VMOP_SPLICE);
        } else {
            compileOperator(op);
        }
    } else if (match(TOKEN_CONTEXT_OPEN)) {
        if (currentToken.value == "{") {
            compileJSONObject();
        } else if (currentToken.value == "(") {
            emitOperation(VMOP_FUN_PUSH);
            nextToken();
        } else {
            compileContextOperator(currentToken.value[0]);
            nextToken();
        }
    } else if (match(TOKEN_CONTEXT_CLOSE)) {
        if (currentToken.value == ")") {
            emitOperation(VMOP_FUN_EVAL);
        } else {
            compileContextOperator(currentToken.value[0]);
        }
        nextToken();
    } else {
        // Skip unknown tokens
        nextToken();
    }
}

void EdictCompiler::compileJSONObject() {
    tokenizer.setJSONMode(true);
    nextToken(); // consume '{'
    emitValue(Value(Dictionary())); // New tree/listree object
    emitOperation(VMOP_CTX_PUSH);
    
    while (currentToken.type != TOKEN_EOF && currentToken.value != "}") {
        std::string key;
        if (match(TOKEN_STRING)) {
            key = currentToken.value.substr(1, currentToken.value.length() - 2);
            nextToken();
        } else if (match(TOKEN_IDENTIFIER)) {
            key = currentToken.value;
            nextToken();
        } else break;
        
        if (match(":")) nextToken();
        else break;
        
        compileJSONValue();
        
        // Assign value to key in current context
        emitValue(Value(key));
        emitOperation(VMOP_ASSIGN);
        
        if (match(",")) nextToken();
    }
    
    expect("}");
    emitOperation(VMOP_CTX_POP);
    tokenizer.setJSONMode(false);
}

void EdictCompiler::compileJSONArray() {
    nextToken(); // consume '['
    emitListStart(); // New listree (LIST mode)
    
    while (currentToken.type != TOKEN_EOF && currentToken.value != "]") {
        compileJSONValue();
        emitOperation(VMOP_LIST_ADD);
        if (match(",")) nextToken();
    }
    
    expect("]");
}

void EdictCompiler::compileJSONValue() {
    if (match(TOKEN_CONTEXT_OPEN) && currentToken.value == "{") {
        compileJSONObject();
    } else if (match(TOKEN_CONTEXT_OPEN) && currentToken.value == "[") {
        compileJSONArray();
    } else if (match(TOKEN_STRING)) {
        std::string s = currentToken.value.substr(1, currentToken.value.length() - 2);
        emitValue(Value(s));
        nextToken();
    } else if (match(TOKEN_IDENTIFIER)) {
        // Treat identifiers as strings in JSON mode
        emitValue(Value(currentToken.value));
        nextToken();
    } else if (match(TOKEN_LITERAL)) {
        // Standard literal logic (parentheses only?)
        // If [] is context open, then literal is just ()?
        std::string literal = currentToken.value.substr(1, currentToken.value.length() - 2);
        emitLiteral(literal);
        nextToken();
    } else {
        nextToken();
    }
}

void EdictCompiler::compileLiteral() {
    // Extract the literal content (without delimiters)
    std::string literal = currentToken.value.substr(1, currentToken.value.length() - 2);
    
    // Emit the literal
    emitLiteral(literal);
    
    nextToken();
}

void EdictCompiler::compileIdentifier() {
    std::string identifier = currentToken.value;
    nextToken();

    if (identifier == "logic" && match(TOKEN_CONTEXT_OPEN) && currentToken.value == "{") {
        compileLogicBlock();
        return;
    }
    if (identifier == "speculate" && match(TOKEN_LITERAL)) {
        compileSpeculateBlock();
        return;
    }
    
    // Check for keywords
    if (identifier == "dup") { output.addOp(VMOP_DUP); }
    else if (identifier == "swap") { output.addOp(VMOP_SWAP); }
    else if (identifier == "pop") { output.addOp(VMOP_POP); }
    else if (identifier == "print") { output.addOp(VMOP_PRINT); }
    else if (identifier == "fail") { output.addOp(VMOP_FAIL); }
    else if (identifier == "test") { output.addOp(VMOP_TEST); }
    else if (identifier == "unsafe_extensions_allow") { output.addOp(VMOP_UNSAFE_EXTENSIONS_ALLOW); }
    else if (identifier == "unsafe_extensions_block") { output.addOp(VMOP_UNSAFE_EXTENSIONS_BLOCK); }
    else if (identifier == "unsafe_extensions_status") { output.addOp(VMOP_UNSAFE_EXTENSIONS_STATUS); }
    else {
        // Treat as symbol/string - pushing string value and auto-dereference
        // (Numeric strings will be parsed by arithmetic ops at runtime if needed)
        output.addOp(VMOP_PUSHEXT);
        output.addValue(Value(identifier));
        output.addOp(VMOP_REF);
    }
}

void EdictCompiler::compileLogicBlock() {
    compileJSONObject();
    emitOperation(VMOP_LOGIC_RUN);
}

void EdictCompiler::compileSpeculateBlock() {
    std::string literal = currentToken.value.substr(1, currentToken.value.length() - 2);
    emitLiteral(literal);
    emitOperation(VMOP_SPECULATE);
    nextToken();
}

void EdictCompiler::compileOperator(const std::string& op) {
    if (op == "@") {
        // Bare '@' form is written as: key value @
        // VMOP_ASSIGN expects top-of-stack to be key.
        output.addOp(VMOP_SWAP);
        output.addOp(VMOP_ASSIGN);
        return;
    }

    auto it = operatorMap.find(op);
    if (it != operatorMap.end()) {
        output.addOp(it->second);
    } else {
        throw std::runtime_error("Unknown operator: " + op);
    }
}

void EdictCompiler::compileContextOperator(char op) {
    if (op == '{' || op == '<') {
        output.addOp(VMOP_CTX_PUSH);
    } else {
        output.addOp(VMOP_CTX_POP);
    }
}

void EdictCompiler::emitOperation(VMOpcode op) {
    output.addOp(op);
}

void EdictCompiler::emitLiteral(const std::string& literal) {
    // Push the literal as a string
    output.addOp(VMOP_PUSHEXT);
    output.addValue(Value(literal));
}

void EdictCompiler::emitList() {
    output.addOp(VMOP_PUSHEXT);
    output.getData().push_back(VMEXT_LIST);
}

void EdictCompiler::emitValue(const Value& value) {
    output.addOp(VMOP_PUSHEXT);
    output.addValue(value);
}

void EdictCompiler::emitListStart() {
    output.addOp(VMOP_PUSHEXT);
    output.getData().push_back(VMEXT_LIST);
}

} // namespace agentc::edict
