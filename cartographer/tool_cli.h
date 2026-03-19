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

#include <istream>
#include <ostream>
#include <string>

#include "../listree/listree.h"

namespace agentc {
namespace cartographer {
namespace toolcli {

int runParseCommand(CPtr<ListreeValue> args,
                    std::ostream& output,
                    std::ostream& error);
int runResolveCommand(CPtr<ListreeValue> args,
                      std::istream& input,
                      std::ostream& output,
                      std::ostream& error);

} // namespace toolcli
} // namespace cartographer
} // namespace agentc
