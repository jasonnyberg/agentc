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

#include "mapper.h"

namespace agentc {
namespace cartographer {
namespace parser {

bool parseHeaderToDescription(Mapper& mapper,
                              const std::string& headerPath,
                              Mapper::ParseDescription& out,
                              std::string& error);
bool parseHeaderToDescription(const std::string& headerPath,
                              Mapper::ParseDescription& out,
                              std::string& error);

bool parseHeaderToParserJson(Mapper& mapper,
                       const std::string& headerPath,
                       std::string& jsonOut,
                       std::string& error);
bool parseHeaderToParserJson(const std::string& headerPath,
                       std::string& jsonOut,
                       std::string& error);

} // namespace parser
} // namespace cartographer
} // namespace agentc
