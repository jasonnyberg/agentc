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

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace agentc::edict {
namespace {

bool valueToString(CPtr<agentc::ListreeValue> value, std::string& out) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return false;
    }
    if ((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        return false;
    }
    out.assign(static_cast<const char*>(value->getData()), value->getLength());
    return true;
}

CPtr<agentc::ListreeValue> fieldValue(CPtr<agentc::ListreeValue> object,
                                      const std::string& name) {
    if (!object || object->isListMode()) {
        return nullptr;
    }
    auto item = object->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

bool extractHandleId(CPtr<agentc::ListreeValue> object,
                     const std::string& field,
                     std::string& out) {
    return valueToString(fieldValue(object, field), out);
}

bool listToStrings(CPtr<agentc::ListreeValue> value,
                   std::vector<std::string>& out) {
    if (!value || !value->isListMode()) {
        return false;
    }
    out.clear();
    value->forEachList([&](CPtr<ListreeValueRef>& ref) {
        auto current = ref ? ref->getValue() : nullptr;
        std::string text;
        if (valueToString(current, text)) {
            out.push_back(text);
        } else if (!current) {
            out.emplace_back();
        } else {
            out.push_back(agentc::toJson(current));
        }
    });
    return true;
}

long long parseLongLong(CPtr<agentc::ListreeValue> value,
                        long long fallback,
                        bool* ok = nullptr) {
    std::string text;
    if (!valueToString(value, text)) {
        if (ok) *ok = false;
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || (end && *end != '\0') || errno != 0) {
        if (ok) *ok = false;
        return fallback;
    }
    if (ok) *ok = true;
    return parsed;
}

CPtr<agentc::ListreeValue> stringListValue(const std::vector<std::string>& values) {
    auto list = agentc::createListValue();
    for (const auto& value : values) {
        agentc::addListItem(list, agentc::createStringValue(value));
    }
    return list;
}

CPtr<agentc::ListreeValue> envelopeValue(const tcc::TccEnvelope& envelope) {
    auto value = agentc::createNullValue();
    agentc::addNamedItem(value, "status", agentc::createStringValue(envelope.status));
    agentc::addNamedItem(value, "ok", agentc::createStringValue(envelope.ok ? "true" : "false"));
    agentc::addNamedItem(value, "available", agentc::createStringValue(envelope.available ? "true" : "false"));
    if (!envelope.error.empty()) {
        agentc::addNamedItem(value, "error", agentc::createStringValue(envelope.error));
    }
    if (!envelope.moduleId.empty()) {
        agentc::addNamedItem(value, "module_id", agentc::createStringValue(envelope.moduleId));
    }
    if (!envelope.jobId.empty()) {
        agentc::addNamedItem(value, "job_id", agentc::createStringValue(envelope.jobId));
    }
    if (!envelope.entrySymbol.empty()) {
        agentc::addNamedItem(value, "entry_symbol", agentc::createStringValue(envelope.entrySymbol));
    }
    if (!envelope.resultKind.empty()) {
        agentc::addNamedItem(value, "result_kind", agentc::createStringValue(envelope.resultKind));
    }
    if (!envelope.resultText.empty()) {
        agentc::addNamedItem(value, "result_text", agentc::createStringValue(envelope.resultText));
    }
    if (envelope.resultKind == "i64") {
        agentc::addNamedItem(value, "result_i64", agentc::createStringValue(std::to_string(envelope.resultI64)));
    }
    if (envelope.resultKind == "f64") {
        agentc::addNamedItem(value, "result_f64", agentc::createStringValue(std::to_string(envelope.resultF64)));
    }
    if (envelope.signalNumber != 0) {
        agentc::addNamedItem(value, "signal", agentc::createStringValue(std::to_string(envelope.signalNumber)));
    }
    if (envelope.exitCode != 0) {
        agentc::addNamedItem(value, "exit_code", agentc::createStringValue(std::to_string(envelope.exitCode)));
    }
    if (envelope.pid > 0) {
        agentc::addNamedItem(value, "pid", agentc::createStringValue(std::to_string(envelope.pid)));
    }
    if (envelope.timeoutMs > 0) {
        agentc::addNamedItem(value, "timeout_ms", agentc::createStringValue(std::to_string(envelope.timeoutMs)));
    }
    if (envelope.symbolCount > 0) {
        agentc::addNamedItem(value, "symbol_count", agentc::createStringValue(std::to_string(envelope.symbolCount)));
    }
    if (!envelope.handleKind.empty()) {
        agentc::addNamedItem(value, "handle_kind", agentc::createStringValue(envelope.handleKind));
    }
    if (!envelope.launchMode.empty()) {
        agentc::addNamedItem(value, "launch_mode", agentc::createStringValue(envelope.launchMode));
    }
    if (!envelope.diagnostics.empty()) {
        agentc::addNamedItem(value, "diagnostics", stringListValue(envelope.diagnostics));
    }
    if (!envelope.symbols.empty()) {
        agentc::addNamedItem(value, "symbols", stringListValue(envelope.symbols));
    }
    if (!envelope.logs.empty()) {
        agentc::addNamedItem(value, "logs", stringListValue(envelope.logs));
    }
    return value;
}

} // namespace

void EdictVM::op_TCC_AVAILABLE() {
    pushData(envelopeValue(tccService_->availability()));
}

void EdictVM::op_TCC_COMPILE() {
    auto sourceValue = popData();
    std::string source;
    if (!valueToString(sourceValue, source)) {
        setError("tcc.compile requires a source string");
        return;
    }
    pushData(envelopeValue(tccService_->compile(source)));
}

void EdictVM::op_TCC_RUN() {
    auto argsValue = popData();
    auto moduleValue = popData();

    std::vector<std::string> args;
    if (!listToStrings(argsValue, args)) {
        setError("tcc.run requires a list of string arguments");
        return;
    }

    std::string moduleId;
    if (!extractHandleId(moduleValue, "module_id", moduleId)) {
        setError("tcc.run requires a module handle envelope with module_id");
        return;
    }

    pushData(envelopeValue(tccService_->run(moduleId, args)));
}

void EdictVM::op_TCC_SYMBOLS() {
    auto moduleValue = popData();
    std::string moduleId;
    if (!extractHandleId(moduleValue, "module_id", moduleId)) {
        setError("tcc.symbols requires a module handle envelope with module_id");
        return;
    }
    pushData(envelopeValue(tccService_->listSymbols(moduleId)));
}

void EdictVM::op_TCC_DROP() {
    auto moduleValue = popData();
    std::string moduleId;
    if (!extractHandleId(moduleValue, "module_id", moduleId)) {
        setError("tcc.drop requires a module handle envelope with module_id");
        return;
    }
    pushData(envelopeValue(tccService_->drop(moduleId)));
}

void EdictVM::op_TCC_START_ISOLATED() {
    auto timeoutValue = popData();
    auto argsValue = popData();
    auto moduleValue = popData();

    std::vector<std::string> args;
    if (!listToStrings(argsValue, args)) {
        setError("tcc.start_isolated requires a list of string arguments");
        return;
    }

    bool timeoutOk = false;
    const long long timeoutMs = parseLongLong(timeoutValue, 0, &timeoutOk);
    if (!timeoutOk) {
        setError("tcc.start_isolated requires a numeric timeout string");
        return;
    }

    std::string moduleId;
    if (!extractHandleId(moduleValue, "module_id", moduleId)) {
        setError("tcc.start_isolated requires a module handle envelope with module_id");
        return;
    }

    pushData(envelopeValue(tccService_->startIsolated(moduleId, args, timeoutMs)));
}

void EdictVM::op_TCC_STATUS() {
    auto jobValue = popData();
    std::string jobId;
    if (!extractHandleId(jobValue, "job_id", jobId)) {
        setError("tcc.status requires a job handle envelope with job_id");
        return;
    }
    pushData(envelopeValue(tccService_->status(jobId)));
}

void EdictVM::op_TCC_COLLECT() {
    auto jobValue = popData();
    std::string jobId;
    if (!extractHandleId(jobValue, "job_id", jobId)) {
        setError("tcc.collect requires a job handle envelope with job_id");
        return;
    }
    pushData(envelopeValue(tccService_->collect(jobId)));
}

void EdictVM::op_TCC_CANCEL() {
    auto jobValue = popData();
    std::string jobId;
    if (!extractHandleId(jobValue, "job_id", jobId)) {
        setError("tcc.cancel requires a job handle envelope with job_id");
        return;
    }
    pushData(envelopeValue(tccService_->cancel(jobId)));
}

void EdictVM::op_TCC_ALLOW_PROCESS_SYMBOL() {
    auto declarationValue = popData();
    auto nameValue = popData();

    std::string name;
    std::string declaration;
    if (!valueToString(nameValue, name) || !valueToString(declarationValue, declaration)) {
        setError("tcc.allow_process_symbol requires name and declaration strings");
        return;
    }

    pushData(envelopeValue(tccService_->allowProcessSymbol(name, declaration)));
}

void EdictVM::op_TCC_ALLOW_LIBRARY_SYMBOL() {
    auto declarationValue = popData();
    auto nameValue = popData();
    auto pathValue = popData();

    std::string libraryPath;
    std::string name;
    std::string declaration;
    if (!valueToString(pathValue, libraryPath) ||
        !valueToString(nameValue, name) ||
        !valueToString(declarationValue, declaration)) {
        setError("tcc.allow_library_symbol requires library path, name, and declaration strings");
        return;
    }

    pushData(envelopeValue(tccService_->allowLibrarySymbol(libraryPath, name, declaration)));
}

void EdictVM::op_TCC_CLEAR_SYMBOLS() {
    pushData(envelopeValue(tccService_->clearAllowedSymbols()));
}

} // namespace agentc::edict
