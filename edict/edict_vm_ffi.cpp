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

#include "edict_vm.h"
#include "../cartographer/mapper.h"
#include "../cartographer/ffi.h"
#include "../cartographer/parser.h"
#include "../cartographer/protocol.h"
#include "../cartographer/resolver.h"
#include "../cartographer/service.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace agentc::edict {

namespace {

bool valueToString(CPtr<agentc::ListreeValue> v, std::string& out) {
    if (!v || !v->getData() || v->getLength() == 0) return false;
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return false;
    out.assign(static_cast<char*>(v->getData()), v->getLength());
    return true;
}

std::vector<CPtr<agentc::ListreeValue>> listValues(CPtr<agentc::ListreeValue> value) {
    std::vector<CPtr<agentc::ListreeValue>> out;
    if (!value || !value->isListMode()) {
        return out;
    }

    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            out.push_back(ref->getValue());
        }
    });
    std::reverse(out.begin(), out.end());
    return out;
}

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value, const std::string& name) {
    if (!value) {
        return nullptr;
    }

    if (value->isListMode()) {
        for (const auto& entry : listValues(value)) {
            auto pair = listValues(entry);
            if (pair.size() != 2) {
                continue;
            }
            std::string key;
            if (valueToString(pair[0], key) && key == name) {
                return pair[1];
            }
        }
        return nullptr;
    }

    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string stringField(CPtr<agentc::ListreeValue> value, const std::string& name) {
    std::string out;
    valueToString(namedValue(value, name), out);
    return out;
}

bool importRequestId(CPtr<agentc::ListreeValue> value, std::string& out) {
    out = stringField(value, "request_id");
    if (!out.empty()) {
        return true;
    }
    if (valueToString(value, out)) {
        return true;
    }
    return false;
}

// Push a library-definition tree onto the data stack.
// NOTE (G058): Automatic read-only freeze is intentionally disabled here —
// callers commonly mutate the defs immediately after import
// (e.g. normalize_thread_runtime_defs in tests).  When the caller is done
// mutating, they may call defs->setReadOnly(true) themselves before sharing
// the tree across threads.
void pushFrozenDefinitions(agentc::edict::EdictVM* vm,
                           CPtr<agentc::ListreeValue> defs) {
    vm->pushData(defs);
}

} // namespace

void EdictVM::op_MAP() {
    auto v = popData();
    std::string path;
    if (!valueToString(v, path)) { setError("MAP expects string path"); return; }
    auto t0 = std::chrono::steady_clock::now();
    auto defs = mapper->parse(path);
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    if (defs) {
        pushData(defs);
        std::cout << "[map] " << path << " parsed in " << us << " us" << std::endl;
    } else {
        pushData(agentc::createNullValue());
        std::cout << "[map] " << path << " parse failed in " << us << " us" << std::endl;
    }
}

void EdictVM::op_LOAD() {
    auto v = popData();
    std::string path;
    if (!valueToString(v, path)) { setError("LOAD expects string path"); return; }
    auto t0 = std::chrono::steady_clock::now();
    bool ok = ffi->loadLibrary(path);
    auto t1 = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    if (!ok) { setError("Failed to load library: " + path); return; }
    std::cout << "[load] " << path << " loaded in " << us << " us" << std::endl;
}

void EdictVM::op_IMPORT() {
    auto headerValue = popData();
    auto libraryValue = popData();

    std::string headerPath;
    std::string libraryPath;
    if (!valueToString(headerValue, headerPath) || !valueToString(libraryValue, libraryPath)) {
        setError("IMPORT expects library path and header path strings");
        return;
    }

    if (!cartographer) {
        setError("Cartographer service not initialized");
        return;
    }

    // Check for cached import information
    std::filesystem::path libP(libraryPath.c_str());
    std::filesystem::path hdrP(headerPath.c_str());
    std::string cacheFileName = libP.filename().string() + "_" + hdrP.filename().string() + ".json";
    const char* homeDir = std::getenv("HOME");
    std::filesystem::path cacheDir = homeDir ? std::filesystem::path(std::string(homeDir) + "/.cache/agentc") : std::filesystem::path(".agentc/cache");
    
    std::error_code ecDir;
    std::filesystem::create_directories(cacheDir, ecDir);

    std::filesystem::path cachePath = cacheDir / std::filesystem::path(cacheFileName.c_str());

    bool cacheValid = false;
    std::string cachedJson;
    std::error_code ec;

    auto cacheTime = std::filesystem::last_write_time(cachePath, ec);
    if (!ec) {
        auto libTime = std::filesystem::last_write_time(libP, ec);
        if (!ec && cacheTime >= libTime) {
            auto hdrTime = std::filesystem::last_write_time(hdrP, ec);
            if (!ec && cacheTime >= hdrTime) {
                std::ifstream ifs(cachePath);
                if (ifs) {
                    cachedJson.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    cacheValid = !cachedJson.empty();
                }
            }
        }
    }

    if (cacheValid) {
        auto result = cartographer->importResolverJson(cachedJson, cachePath.string());
        if (result.ok) {
            pushFrozenDefinitions(this, result.definitions);
            return;
        }
        // If import fails for some reason (e.g. format mismatch), fall through to regenerate
    }

    // Cache miss or invalid: run full pipeline to regenerate
    std::string schemaJson;
    std::string errorMsg;
    if (!agentc::cartographer::parser::parseHeaderToParserJson(*mapper, headerPath, schemaJson, errorMsg)) {
        setError(errorMsg.empty() ? "Cartographer parse failed" : errorMsg);
        return;
    }

    agentc::cartographer::Mapper::ParseDescription description;
    if (!agentc::cartographer::protocol::decodeParseDescription(schemaJson, description, errorMsg)) {
        setError(errorMsg.empty() ? "Cartographer schema decode failed" : errorMsg);
        return;
    }

    agentc::cartographer::resolver::ResolvedApi resolved;
    if (!agentc::cartographer::resolver::resolveApiDescription(libraryPath, description, resolved, errorMsg)) {
        setError(errorMsg.empty() ? "Cartographer resolve failed" : errorMsg);
        return;
    }

    std::string resolvedJson = agentc::cartographer::resolver::encodeResolvedApi(resolved);

    // Save cache for next time
    std::filesystem::create_directories(cacheDir, ec);
    std::ofstream ofs(cachePath);
    if (ofs) {
        ofs << resolvedJson;
    }

    auto result = cartographer->importResolverJson(resolvedJson, cachePath.string());
    if (!result.ok) {
        setError(result.error.empty() ? "Cartographer cache import failed" : result.error);
        return;
    }

    pushFrozenDefinitions(this, result.definitions);
}

void EdictVM::op_IMPORT_RESOLVED() {
    auto resolvedValue = popData();

    std::string resolvedSchemaPath;
    if (!valueToString(resolvedValue, resolvedSchemaPath)) {
        setError("IMPORT_RESOLVED expects a resolved schema path string");
        return;
    }

    auto result = cartographer ? cartographer->importResolvedFile(resolvedSchemaPath)
                               : agentc::cartographer::ImportResult{};
    if (!result.ok) {
        setError(result.error.empty() ? "Cartographer resolved import failed" : result.error);
        return;
    }

    pushFrozenDefinitions(this, result.definitions);
}

void EdictVM::op_IMPORT_DEFERRED() {
    auto headerValue = popData();
    auto libraryValue = popData();

    std::string headerPath;
    std::string libraryPath;
    if (!valueToString(headerValue, headerPath) || !valueToString(libraryValue, libraryPath)) {
        setError("IMPORT_DEFERRED expects library path and header path strings");
        return;
    }

    agentc::cartographer::ImportRequest request;
    request.libraryPath = libraryPath;
    request.headerPath = headerPath;
    request.executionMode = agentc::cartographer::ImportExecutionMode::Deferred;

    auto result = cartographer ? cartographer->importDeferred(request) : agentc::cartographer::ImportResult{};
    if (!result.ok) {
        setError(result.error.empty() ? "Cartographer deferred import failed" : result.error);
        return;
    }

    pushFrozenDefinitions(this, result.definitions);
}

void EdictVM::op_IMPORT_COLLECT() {
    auto requestValue = popData();
    std::string requestId;
    if (!importRequestId(requestValue, requestId)) {
        setError("IMPORT_COLLECT expects a request id or deferred import handle");
        return;
    }

    auto result = cartographer ? cartographer->collect(requestId) : agentc::cartographer::ImportResult{};
    if (!result.ok) {
        setError(result.error.empty() ? "Cartographer deferred collection failed" : result.error);
        return;
    }

    pushFrozenDefinitions(this, result.definitions);
}

void EdictVM::op_IMPORT_STATUS() {
    auto requestValue = popData();
    std::string requestId;
    if (!importRequestId(requestValue, requestId)) {
        setError("IMPORT_STATUS expects a request id or deferred import handle");
        return;
    }

    auto result = cartographer ? cartographer->importStatus(requestId) : agentc::cartographer::ImportResult{};
    if (result.status == "missing") {
        setError(result.error.empty() ? "Cartographer deferred status failed" : result.error);
        return;
    }

    pushFrozenDefinitions(this, result.definitions);
}

void EdictVM::op_PARSE_JSON() {
    auto headerValue = popData();
    std::string headerPath;
    if (!valueToString(headerValue, headerPath)) {
        setError("PARSE_JSON expects a header path string");
        return;
    }

    std::string schemaJson;
    std::string error;
    if (!agentc::cartographer::parser::parseHeaderToParserJson(*mapper, headerPath, schemaJson, error)) {
        setError(error.empty() ? "Cartographer parser failed" : error);
        return;
    }

    pushData(agentc::createStringValue(schemaJson));
}

void EdictVM::op_MATERIALIZE_JSON() {
    auto schemaValue = popData();
    std::string schemaJson;
    if (!valueToString(schemaValue, schemaJson)) {
        setError("MATERIALIZE_JSON expects schema JSON string");
        return;
    }

    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    if (!agentc::cartographer::protocol::decodeParseDescription(schemaJson, description, error)) {
        setError(error.empty() ? "Cartographer schema decode failed" : error);
        return;
    }

    pushData(agentc::cartographer::Mapper::materialize(description));
}

void EdictVM::op_RESOLVE_JSON() {
    auto schemaValue = popData();
    auto libraryValue = popData();

    std::string schemaJson;
    std::string libraryPath;
    if (!valueToString(schemaValue, schemaJson) || !valueToString(libraryValue, libraryPath)) {
        setError("RESOLVE_JSON expects library path and schema JSON strings");
        return;
    }

    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    if (!agentc::cartographer::protocol::decodeParseDescription(schemaJson, description, error)) {
        setError(error.empty() ? "Cartographer schema decode failed" : error);
        return;
    }

    agentc::cartographer::resolver::ResolvedApi resolved;
    if (!agentc::cartographer::resolver::resolveApiDescription(libraryPath, description, resolved, error)) {
        setError(error.empty() ? "Cartographer resolver failed" : error);
        return;
    }

    pushData(agentc::createStringValue(agentc::cartographer::resolver::encodeResolvedApi(resolved)));
}

void EdictVM::op_IMPORT_RESOLVED_JSON() {
    auto resolvedValue = popData();

    std::string resolvedJson;
    if (!valueToString(resolvedValue, resolvedJson)) {
        setError("IMPORT_RESOLVED_JSON expects a resolved schema JSON string");
        return;
    }

    auto result = cartographer ? cartographer->importResolverJson(resolvedJson, "<memory>")
                               : agentc::cartographer::ImportResult{};
    if (!result.ok) {
        setError(result.error.empty() ? "Cartographer in-memory resolved import failed" : result.error);
        return;
    }

    pushFrozenDefinitions(this, result.definitions);
}

void EdictVM::op_READ_TEXT() {
    auto pathValue = popData();
    std::string path;
    if (!valueToString(pathValue, path)) {
        setError("READ_TEXT expects a file path string");
        return;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        setError("Failed to read file: " + path);
        return;
    }

    std::string text((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
    if (input.bad()) {
        setError("Failed to read file: " + path);
        return;
    }

    pushData(agentc::createStringValue(text));
}

void EdictVM::op_REQUEST_ID() {
    auto requestValue = popData();
    std::string requestId;
    if (!importRequestId(requestValue, requestId)) {
        setError("REQUEST_ID expects a request id string or deferred import handle");
        return;
    }

    pushData(agentc::createStringValue(requestId));
}

} // namespace agentc::edict
