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

#include "edict_types.h"
#include <sstream>

namespace agentc::edict {

// Dictionary implementation
void Dictionary::set(const std::string& key, const Value& value) {
    entries[key] = std::make_shared<Value>(value);
}

Value Dictionary::get(const std::string& key) const {
    auto it = entries.find(key);
    return it != entries.end() ? *it->second : Value();
}

bool Dictionary::remove(const std::string& key) {
    return entries.erase(key) > 0;
}

bool Dictionary::contains(const std::string& key) const {
    return entries.find(key) != entries.end();
}

// Value implementation for Dictionary
Value::Value(const Dictionary& dict) 
    : type(ValueType::VALUE_DICTIONARY), 
      dictionaryValue(std::make_shared<Dictionary>(dict)) {}

Dictionary& Value::asDictionary() {
    if (!isDictionary()) throw std::runtime_error("Value is not a Dictionary");
    return *dictionaryValue;
}

const Dictionary& Value::asDictionary() const {
    if (!isDictionary()) throw std::runtime_error("Value is not a Dictionary");
    return *dictionaryValue;
}

// Implementation of Value::toString()
std::string Value::toString() const {
    if (isNull()) return "null";
    if (isBool()) return asBool() ? "true" : "false";
    if (isString()) return asString();
    if (isReference()) return "<reference>";
    if (isDictionary()) {
        std::ostringstream ss;
        ss << "{";
        bool first = true;
        for (const auto& pair : asDictionary()) {
            if (!first) ss << ", ";
            ss << "\"" << pair.first << "\": " << pair.second->toString();
            first = false;
        }
        ss << "}";
        return ss.str();
    }
    return "<unknown>";
}

// Implementation of Dictionary serialization
std::string Dictionary::serialize() const {
    std::ostringstream ss;
    ss << entries.size() << ";";
    for (const auto& pair : entries) {
        ss << pair.first.length() << ":" << pair.first << ";";
        ss << pair.second->toString() << ";";
    }
    return ss.str();
}

Dictionary Dictionary::deserialize(const std::string& data) {
    Dictionary dict;
    size_t pos = 0;
    
    // Read number of entries
    size_t semicolon = data.find(';', pos);
    if (semicolon == std::string::npos) return dict;
    
    size_t count = std::stoul(data.substr(pos, semicolon - pos));
    pos = semicolon + 1;
    
    // Read each entry
    for (size_t i = 0; i < count; i++) {
        // Read key length
        semicolon = data.find(':', pos);
        if (semicolon == std::string::npos) break;
        
        size_t keyLength = std::stoul(data.substr(pos, semicolon - pos));
        pos = semicolon + 1;
        
        // Read key
        std::string key = data.substr(pos, keyLength);
        pos += keyLength;
        
        // Skip semicolon
        if (pos < data.length() && data[pos] == ';') pos++;
        else break;
        
        // Read value
        semicolon = data.find(';', pos);
        if (semicolon == std::string::npos) break;
        
        std::string valueStr = data.substr(pos, semicolon - pos);
        pos = semicolon + 1;
        
        // Parse value (simplified for now)
        Value value = Value(); // default-initialize to null
        if (valueStr == "null") {
            value = Value(nullptr);
        } else if (valueStr == "true") {
            value = Value(true);
        } else if (valueStr == "false") {
            value = Value(false);
        } else {
            value = Value(valueStr);
        }
        
        dict.set(key, value);
    }
    
    return dict;
}

// Implementation of BytecodeBuffer methods
void BytecodeBuffer::addOp(VMOpcode op) {
    data.push_back(static_cast<uint8_t>(op));
}

void BytecodeBuffer::addString(const std::string& value) {
    // Add string length (4 bytes, little-endian)
    int len = static_cast<int>(value.length());
    for (size_t i = 0; i < sizeof(int); i++) {
        data.push_back((len >> (i * 8)) & 0xFF);
    }
    
    // Add string data
    for (char c : value) {
        data.push_back(static_cast<uint8_t>(c));
    }
}

void BytecodeBuffer::addValue(const Value& value) {
    if (value.isNull()) {
        data.push_back(VMEXT_NULL);
    } else if (value.isBool()) {
        data.push_back(VMEXT_BOOL);
        data.push_back(value.asBool() ? 1 : 0);
    } else if (value.isString()) {
        data.push_back(VMEXT_STRING);
        addString(value.asString());
    } else if (value.isDictionary()) {
        data.push_back(VMEXT_DICT);
        addString(value.asDictionary().serialize());
    } else {
        data.push_back(VMEXT_NULL); // Default to null for unsupported types
    }
}

void BytecodeBuffer::clear() {
    data.clear();
}

} // namespace agentc::edict
