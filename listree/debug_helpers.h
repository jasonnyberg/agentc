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
#include <sstream>
#include "../core/debug.h"

namespace agentc {

// Helper class to allow string concatenation in debug macros
class DebugStream {
private:
    std::ostringstream stream;
    DebugLevel level;

public:
    explicit DebugStream(DebugLevel level) : level(level) {}
    
    ~DebugStream() {
        debug_print(level, stream.str());
    }
    
    template<typename T>
    DebugStream& operator<<(const T& value) {
        stream << value;
        return *this;
    }
};

} // namespace agentc

// Debug macros that support string concatenation
#define LISTREE_DEBUG_ERROR() agentc::DebugStream(DEBUG_ERROR)
#define LISTREE_DEBUG_WARNING() agentc::DebugStream(DEBUG_WARNING)
#define LISTREE_DEBUG_INFO() agentc::DebugStream(DEBUG_INFO)
#define LISTREE_DEBUG_DETAIL() agentc::DebugStream(DEBUG_DETAIL)
#define LISTREE_DEBUG_TRACE() agentc::DebugStream(DEBUG_TRACE)
