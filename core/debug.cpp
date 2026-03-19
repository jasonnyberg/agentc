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

#include "debug.h"

DebugLevel currentDebugLevel = DEBUG_INFO;

void debug_print(DebugLevel level, const std::string& message) {
    if (level <= currentDebugLevel) {
        switch (level) {
            case DEBUG_ERROR:
                std::cerr << "ERROR: " << message << std::endl;
                break;
            case DEBUG_WARNING:
                std::cerr << "WARNING: " << message << std::endl;
                break;
            case DEBUG_INFO:
                std::cout << "INFO: " << message << std::endl;
                break;
            case DEBUG_DETAIL:
                std::cout << "DETAIL: " << message << std::endl;
                break;
            case DEBUG_TRACE:
                std::cout << "TRACE: " << message << std::endl;
                break;
            default:
                break;
        }
    }
}
