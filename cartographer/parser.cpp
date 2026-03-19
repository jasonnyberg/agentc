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

#include "parser.h"

#include "protocol.h"

namespace agentc {
namespace cartographer {
namespace parser {

bool parseHeaderToDescription(Mapper& mapper,
                              const std::string& headerPath,
                              Mapper::ParseDescription& out,
                              std::string& error) {
    if (headerPath.empty()) {
        error = "Cartographer parser requires a header path";
        return false;
    }
    if (!mapper.parseDescription(headerPath, out)) {
        error = "Failed to map header: " + headerPath;
        return false;
    }
    return true;
}

bool parseHeaderToDescription(const std::string& headerPath,
                              Mapper::ParseDescription& out,
                              std::string& error) {
    Mapper mapper;
    return parseHeaderToDescription(mapper, headerPath, out, error);
}

bool parseHeaderToParserJson(Mapper& mapper,
                       const std::string& headerPath,
                       std::string& jsonOut,
                       std::string& error) {
    Mapper::ParseDescription description;
    if (!parseHeaderToDescription(mapper, headerPath, description, error)) {
        return false;
    }
    jsonOut = protocol::encodeParseDescription(description);
    return true;
}

bool parseHeaderToParserJson(const std::string& headerPath,
                       std::string& jsonOut,
                       std::string& error) {
    Mapper mapper;
    return parseHeaderToParserJson(mapper, headerPath, jsonOut, error);
}

} // namespace parser
} // namespace cartographer
} // namespace agentc
