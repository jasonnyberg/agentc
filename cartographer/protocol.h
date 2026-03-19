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
#include "service.h"

namespace agentc {
namespace cartographer {
namespace protocol {

const char* versionName();

std::string encodeImportRequest(const ImportRequest& request);
bool decodeImportRequest(const std::string& message, ImportRequest& out, std::string& error);

std::string encodeImportStatus(const ImportRequest& request,
                               const ImportResult& result,
                               const Mapper::ParseDescription* description = nullptr);
bool decodeImportStatus(const std::string& message,
                        ImportRequest& requestOut,
                        ImportResult& resultOut,
                        Mapper::ParseDescription* descriptionOut,
                        std::string& error);

std::string encodeParseDescription(const Mapper::ParseDescription& description);
bool decodeParseDescription(const std::string& encoded,
                            Mapper::ParseDescription& out,
                            std::string& error);

const char* parserSchemaFormatName();

} // namespace protocol
} // namespace cartographer
} // namespace agentc
