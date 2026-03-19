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

#include <iostream>
#include <string>

#include "tool_cli.h"

int main(int argc, char** argv) {
    CPtr<agentc::ListreeValue> args = agentc::createListValue();
    for (int i = 1; i < argc; ++i) {
        args->put(agentc::createStringValue(argv[i]), false);
    }
    return agentc::cartographer::toolcli::runResolveCommand(args, std::cin, std::cout, std::cerr);
}
