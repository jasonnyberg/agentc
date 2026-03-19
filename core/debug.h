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
#include <iostream>

enum DebugLevel {
    DEBUG_NONE = 0,
    DEBUG_ERROR,
    DEBUG_WARNING,
    DEBUG_INFO,
    DEBUG_DETAIL,
    DEBUG_TRACE
};

extern DebugLevel currentDebugLevel;

namespace agentc::log {
    inline bool alloc = false;
    inline bool vm = false;
    inline bool kanren = false;
    inline bool ffi = false;
    inline bool cursor = false;
}

void debug_print(DebugLevel level, const std::string& message);

#define LOG_VM(...) do { if (agentc::log::vm) { std::cerr << "VM: " << __VA_ARGS__ << "\n"; } } while(0)
#define LOG_ALLOC(...) do { if (agentc::log::alloc) { std::cerr << "ALLOC: " << __VA_ARGS__ << "\n"; } } while(0)
#define LOG_KANREN(...) do { if (agentc::log::kanren) { std::cerr << "KANREN: " << __VA_ARGS__ << "\n"; } } while(0)
#define LOG_FFI(...) do { if (agentc::log::ffi) { std::cerr << "FFI: " << __VA_ARGS__ << "\n"; } } while(0)
#define LOG_CURSOR(...) do { if (agentc::log::cursor) { std::cerr << "CURSOR: " << __VA_ARGS__ << "\n"; } } while(0)

#define DEBUG_ERROR(msg) debug_print(DEBUG_ERROR, msg)
#define DEBUG_WARNING(msg) debug_print(DEBUG_WARNING, msg)
#define DEBUG_INFO(msg) debug_print(DEBUG_INFO, msg)
#define DEBUG_DETAIL(msg) debug_print(DEBUG_DETAIL, msg)
#define DEBUG_TRACE(msg) debug_print(DEBUG_TRACE, msg)
