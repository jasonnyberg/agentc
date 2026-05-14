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

namespace agentc::edict {

// Implementation of Value::toString()
std::string Value::toString() const {
    if (isNull()) return "null";
    if (isBool()) return asBool() ? "true" : "false";
    if (isString()) return asString();
    if (isReference()) return "<reference>";
    return "<unknown>";
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
    } else {
        data.push_back(VMEXT_NULL); // Default to null for unsupported types
    }
}

void BytecodeBuffer::clear() {
    data.clear();
}

} // namespace agentc::edict
