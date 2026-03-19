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

#include "tool_cli.h"

#include "parser.h"
#include "protocol.h"
#include "resolver.h"

#include <fstream>
#include <iterator>

namespace agentc {
namespace cartographer {
namespace toolcli {

namespace {

static size_t argumentCount(CPtr<ListreeValue> args) {
    size_t count = 0;
    if (!args) return count;
    args->forEachList([&count](CPtr<ListreeValueRef>&) { ++count; });
    return count;
}

static std::string argumentAt(CPtr<ListreeValue> args, size_t index) {
    if (!args) return std::string();
    size_t currentIndex = 0;
    std::string result;
    args->forEachList([&](CPtr<ListreeValueRef>& ref) {
        CPtr<ListreeValue> value = ref ? ref->getValue() : nullptr;
        if (currentIndex == index && value && value->getData() && value->getLength() > 0) {
            result.assign(static_cast<const char*>(value->getData()), value->getLength());
        }
        ++currentIndex;
    });
    return result;
}

static bool readSchemaInput(const std::string& schemaPath,
                            std::istream& input,
                            std::string& out,
                            std::string& error) {
    if (schemaPath == "-") {
        out.assign((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (out.empty() && input.bad()) {
            error = "Failed to read API schema JSON from stdin";
            return false;
        }
        return true;
    }

    std::ifstream schemaFile(schemaPath);
    if (!schemaFile) {
        error = "Failed to read API schema file: " + schemaPath;
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(schemaFile)), std::istreambuf_iterator<char>());
    return true;
}

} // namespace

int runParseCommand(CPtr<ListreeValue> args,
                    std::ostream& output,
                    std::ostream& error) {
    if (argumentCount(args) != 1) {
        error << "Usage: cartographer_parse <header-path>\n";
        return 1;
    }

    std::string json;
    std::string parseError;
    if (!parser::parseHeaderToParserJson(argumentAt(args, 0), json, parseError)) {
        error << parseError << '\n';
        return 1;
    }

    output << json << '\n';
    return 0;
}

int runResolveCommand(CPtr<ListreeValue> args,
                      std::istream& input,
                      std::ostream& output,
                      std::ostream& error) {
    bool includeAddresses = true;
    size_t argIndex = 0;
    if (argumentCount(args) > 0 && argumentAt(args, 0) == "--no-addresses") {
        includeAddresses = false;
        argIndex = 1;
    }

    if (argumentCount(args) < argIndex || argumentCount(args) - argIndex != 2) {
        error << "Usage: cartographer_resolve [--no-addresses] <library-path> <api-schema-json-path|->\n";
        return 1;
    }

    const std::string libraryPath = argumentAt(args, argIndex);
    const std::string schemaPath = argumentAt(args, argIndex + 1);

    std::string schemaJson;
    std::string ioError;
    if (!readSchemaInput(schemaPath, input, schemaJson, ioError)) {
        error << ioError << '\n';
        return 1;
    }

    Mapper::ParseDescription description;
    std::string resolveError;
    if (!protocol::decodeParseDescription(schemaJson, description, resolveError)) {
        error << resolveError << '\n';
        return 1;
    }

    resolver::ResolvedApi resolved;
    if (!resolver::resolveApiDescription(libraryPath, description, resolved, resolveError, includeAddresses)) {
        error << resolveError << '\n';
        return 1;
    }

    output << resolver::encodeResolvedApi(resolved) << '\n';
    return 0;
}

} // namespace toolcli
} // namespace cartographer
} // namespace agentc
