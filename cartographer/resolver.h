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

#include <cstdint>
#include <string>
#include <functional>

#include "../core/container.h"
#include "mapper.h"

namespace agentc {
namespace cartographer {
namespace resolver {

struct ResolvedSymbol {
    std::string key;
    std::string kind;
    std::string name;
    std::string symbolName;
    std::string resolutionStatus;
    std::string address;
};

struct ResolvedApi {
    std::string libraryPath;
    uint64_t fileSize = 0;
    uint64_t modifiedTimeNs = 0;
    std::string contentHash;
    bool addressBindingsProcessLocal = true;
    std::string parserSchemaFormat;
    std::string parserSchemaJson;
    size_t symbolCount = 0;
    size_t resolvedCount = 0;
    size_t unresolvedCount = 0;
    CPtr<CLL<ResolvedSymbol>> symbols;

    void clearSymbols();
    void appendSymbol(const ResolvedSymbol& symbol);
    bool hasSymbols() const;
    CPtr<ResolvedSymbol> firstSymbol() const;
    void forEachSymbol(const std::function<void(CPtr<ResolvedSymbol>&)>& callback) const;
};

const char* resolverSchemaFormatName();
bool resolveApiDescription(const std::string& libraryPath,
                           const Mapper::ParseDescription& description,
                           ResolvedApi& out,
                           std::string& error,
                           bool includeAddresses = true);
std::string encodeResolvedApi(const ResolvedApi& api);
bool decodeResolvedApi(const std::string& json, ResolvedApi& out, std::string& error);

bool validateLibraryFreshness(const std::string& path,
                              uint64_t expectedSize,
                              uint64_t expectedMtimeNs,
                              const std::string& expectedHash,
                              std::string& error);

} // namespace resolver
} // namespace cartographer
} // namespace agentc
