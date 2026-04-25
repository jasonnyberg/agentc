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
#include "edict_compiler.h"
#include "../cartographer/mapper.h"
#include "../cartographer/ffi.h"
#include "../cartographer/ltv_api.h"
#include "../cartographer/parser.h"
#include "../cartographer/protocol.h"
#include "../cartographer/resolver.h"
#include "../cartographer/service.h"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include <unordered_map>

namespace agentc::edict {

static bool edictTraceEnabled() {
    static const bool enabled = []() {
        const char* value = std::getenv("EDICT_TRACE");
        return value && std::string(value) == "1";
    }();
    return enabled;
}

static bool valueToString(CPtr<agentc::ListreeValue> v, std::string& out) {
    if (!v || !v->getData() || v->getLength() == 0) return false;
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return false;
    out.assign(static_cast<char*>(v->getData()), v->getLength());
    return true;
}

static std::string formatValueForDisplay(CPtr<agentc::ListreeValue> v) {
    if (!v) return "<null>";
    if (v->isListMode()) {
        size_t count = 0;
        v->forEachList([&](CPtr<agentc::ListreeValueRef>&){ count++; });
        return "<list:" + std::to_string(count) + ">";
    }
    if ((v->getFlags() & agentc::LtvFlags::Null) != agentc::LtvFlags::None) return "<null>";
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        if (v->getLength() == sizeof(int)) return std::to_string(*(int*)v->getData());
        return "<binary:" + std::to_string(v->getLength()) + ">";
    }
    std::string s;
    if (valueToString(v, s)) return s;
    if (v->getLength() == 0) return "<empty>";
    return "<value>";
}

static bool isRewriteWildcard(const std::string& token) {
    if (token.size() < 2 || token[0] != '$') {
        return false;
    }

    for (size_t i = 1; i < token.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(token[i]))) {
            return false;
        }
    }
    return true;
}

static bool isRewriteTypeToken(const std::string& token) {
    return token == "#list" || token == "#atom";
}

static bool rewriteTypeMatches(const std::string& token, CPtr<agentc::ListreeValue> value) {
    if (!value) {
        return false;
    }
    if (token == "#list") {
        return value->isListMode();
    }
    if (token == "#atom") {
        return !value->isListMode();
    }
    return false;
}

static std::string rewriteModeName(EdictVM::RewriteMode mode) {
    switch (mode) {
        case EdictVM::RewriteMode::Auto: return "auto";
        case EdictVM::RewriteMode::Manual: return "manual";
        case EdictVM::RewriteMode::Off: return "off";
    }
    return "auto";
}

static bool parseRewriteMode(CPtr<agentc::ListreeValue> value, EdictVM::RewriteMode& out) {
    std::string text;
    if (!valueToString(value, text)) {
        return false;
    }
    if (text == "auto") {
        out = EdictVM::RewriteMode::Auto;
        return true;
    }
    if (text == "manual") {
        out = EdictVM::RewriteMode::Manual;
        return true;
    }
    if (text == "off") {
        out = EdictVM::RewriteMode::Off;
        return true;
    }
    return false;
}

static CPtr<agentc::ListreeValue> createRewriteRuleValue(size_t index,
                                                     const std::vector<std::string>& pattern,
                                                     const std::vector<std::string>& replacement) {
    auto out = agentc::createNullValue();
    agentc::addNamedItem(out, "index", agentc::createStringValue(std::to_string(index)));

    auto patternValue = agentc::createListValue();
    for (const auto& token : pattern) {
        agentc::addListItem(patternValue, agentc::createStringValue(token));
    }
    agentc::addNamedItem(out, "pattern", patternValue);

    auto replacementValue = agentc::createListValue();
    for (const auto& token : replacement) {
        agentc::addListItem(replacementValue, agentc::createStringValue(token));
    }
    agentc::addNamedItem(out, "replacement", replacementValue);
    return out;
}

static CPtr<agentc::ListreeValue> createRewriteTraceValue(const std::string& status,
                                                      const std::string& reason,
                                                      const std::string& mode,
                                                      const std::optional<size_t>& index = std::nullopt,
                                                      const std::vector<std::string>* pattern = nullptr,
                                                      const std::vector<std::string>* replacement = nullptr) {
    auto out = agentc::createNullValue();
    agentc::addNamedItem(out, "status", agentc::createStringValue(status));
    agentc::addNamedItem(out, "reason", agentc::createStringValue(reason));
    agentc::addNamedItem(out, "mode", agentc::createStringValue(mode));
    if (index.has_value()) {
        agentc::addNamedItem(out, "index", agentc::createStringValue(std::to_string(*index)));
    }
    if (pattern) {
        auto patternValue = agentc::createListValue();
        for (const auto& token : *pattern) {
            agentc::addListItem(patternValue, agentc::createStringValue(token));
        }
        agentc::addNamedItem(out, "pattern", patternValue);
    }
    if (replacement) {
        auto replacementValue = agentc::createListValue();
        for (const auto& token : *replacement) {
            agentc::addListItem(replacementValue, agentc::createStringValue(token));
        }
        agentc::addNamedItem(out, "replacement", replacementValue);
    }
    return out;
}

struct SpeculativeSnapshot {
    enum class Kind {
        None,
        Null,
        String,
        Binary,
    } kind = Kind::None;

    std::string text;
    std::string binaryBytes;
};

static SpeculativeSnapshot captureSpeculativeSnapshot(CPtr<agentc::ListreeValue> value) {
    SpeculativeSnapshot snapshot;
    if (!value) {
        return snapshot;
    }
    if (value->isListMode()) {
        snapshot.kind = SpeculativeSnapshot::Kind::String;
        snapshot.text = formatValueForDisplay(value);
        return snapshot;
    }
    if ((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        snapshot.kind = SpeculativeSnapshot::Kind::Binary;
        if (value->getLength() > 0) {
            snapshot.binaryBytes.assign(
                static_cast<const char*>(value->getData()),
                value->getLength());
        }
        return snapshot;
    }
    if ((value->getFlags() & agentc::LtvFlags::Null) != agentc::LtvFlags::None) {
        snapshot.kind = SpeculativeSnapshot::Kind::Null;
        return snapshot;
    }
    snapshot.kind = SpeculativeSnapshot::Kind::String;
    snapshot.text = value->getData()
        ? std::string(static_cast<char*>(value->getData()), value->getLength())
        : formatValueForDisplay(value);
    return snapshot;
}

static CPtr<agentc::ListreeValue> materializeSpeculativeSnapshot(const SpeculativeSnapshot& snapshot) {
    switch (snapshot.kind) {
        case SpeculativeSnapshot::Kind::None:
            return nullptr;
        case SpeculativeSnapshot::Kind::Null:
            return agentc::createNullValue();
        case SpeculativeSnapshot::Kind::String:
            return agentc::createStringValue(snapshot.text);
        case SpeculativeSnapshot::Kind::Binary:
            return agentc::createBinaryValue(snapshot.binaryBytes.data(), snapshot.binaryBytes.size());
    }
    return nullptr;
}

static bool parseRewriteIndex(CPtr<agentc::ListreeValue> value, size_t& index) {
    std::string text;
    if (!valueToString(value, text) || text.empty()) {
        return false;
    }

    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    index = static_cast<size_t>(std::strtoul(text.c_str(), nullptr, 10));
    return true;
}

static size_t listValueCount(CPtr<agentc::ListreeValue> value) {
    size_t count = 0;
    if (!value || !value->isListMode()) {
        return 0;
    }
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            ++count;
        }
    });
    return count;
}

static CPtr<agentc::ListreeValue> listValueAt(CPtr<agentc::ListreeValue> value, size_t index) {
    if (!value || !value->isListMode()) {
        return nullptr;
    }

    size_t current = 0;
    CPtr<agentc::ListreeValue> out = nullptr;
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (out || !ref || !ref->getValue()) {
            return;
        }
        if (current == index) {
            out = ref->getValue();
            return;
        }
        ++current;
    }, false);
    return out;
}

static CPtr<agentc::ListreeValue> copyListValue(CPtr<agentc::ListreeValue> value) {
    auto out = agentc::createListValue();
    if (!value || !value->isListMode()) {
        return out;
    }

    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            agentc::addListItem(out, ref->getValue());
        }
    });
    return out;
}

static std::vector<CPtr<agentc::ListreeValue>> listValues(CPtr<agentc::ListreeValue> value);

static bool stringListFromValue(CPtr<agentc::ListreeValue> value,
                                std::vector<std::string>& out,
                                std::string& error,
                                const std::string& fieldName) {
    auto items = listValues(value);
    if (!value || !value->isListMode()) {
        error = fieldName + " must be a list";
        return false;
    }

    out.clear();
    for (const auto& item : items) {
        std::string text;
        if (!valueToString(item, text)) {
            error = fieldName + " entries must be strings";
            return false;
        }
        out.push_back(text);
    }
    return true;
}

static std::vector<CPtr<agentc::ListreeValue>> listValues(CPtr<agentc::ListreeValue> value) {
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

static CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value, const std::string& name) {
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

static std::string stringField(CPtr<agentc::ListreeValue> value, const std::string& name) {
    std::string out;
    valueToString(namedValue(value, name), out);
    return out;
}

static bool importRequestId(CPtr<agentc::ListreeValue> value, std::string& out) {
    out = stringField(value, "request_id");
    if (!out.empty()) {
        return true;
    }
    if (valueToString(value, out)) {
        return true;
    }
    return false;
}

static const char* unsafeExtensionsStatusName(bool allow) {
    return allow ? "allow" : "block";
}

static void addBootstrapMetadata(CPtr<agentc::ListreeValue> object,
                                 const std::string& componentName) {
    if (!object) {
        return;
    }

    auto metadata = agentc::createNullValue();
    agentc::addNamedItem(metadata, "component", agentc::createStringValue(componentName));
    agentc::addNamedItem(metadata, "binding_mode", agentc::createStringValue("startup_curated"));
    agentc::addNamedItem(metadata, "imported_via", agentc::createStringValue("bootstrap_capsule"));
    agentc::addNamedItem(metadata, "service", agentc::createStringValue("cartographer"));
    agentc::addNamedItem(object, "__cartographer", metadata);
}

EdictVM::EdictVM(CPtr<agentc::ListreeValue> root)
    : cursor(root),
      state(VM_NORMAL),
      instruction_ptr(0),
      code_ptr(nullptr),
      code_size(0) {
    mapper = std::make_unique<agentc::cartographer::Mapper>();
    ffi = std::make_unique<agentc::cartographer::FFI>();
    cartographer = std::make_unique<agentc::cartographer::CartographerService>(*mapper, *ffi);
    initResources(root);
}

EdictVM::~EdictVM() = default;

EdictVM::TransactionCheckpoint EdictVM::beginTransaction() {
    return beginTransaction(true);
}

EdictVM::TransactionCheckpoint EdictVM::beginTransaction(bool restoreCodeResource) {
    EdictVM::TransactionCheckpoint checkpoint;
    checkpoint.restoreCodeResource = restoreCodeResource;

    // Capture deep copies of all resource root nodes before taking slab
    // watermarks.  These copies serve as stable pre-mutation snapshots: the
    // slab watermark reclaims nodes allocated *after* this point, so these
    // pre-watermark copies survive rollback and can be written back to
    // restore the resource CPtr members.
    //
    // Note: on commit the copies are slab-resident orphans (no individual
    // dealloc is possible).  This is an accepted trade-off — slab bloat per
    // committed transaction is bounded by resource tree size and reclaimed
    // only on full VM reset.  (Tracked as a known limitation in G006.)
    for (size_t i = 0; i < VMRES_COUNT; ++i) {
        if (i == VMRES_CODE && !restoreCodeResource) {
            checkpoint.resources[i] = resources[i];
            continue;
        }
        checkpoint.resources[i] = resources[i] ? resources[i]->copy() : nullptr;
    }
    checkpoint.rewriteRules.reserve(rewrite_rules.size());
    for (const auto& rule : rewrite_rules) {
        checkpoint.rewriteRules.emplace_back(rule.pattern, rule.replacement);
    }

    checkpoint.savedState = state;
    checkpoint.savedInstructionPtr = instruction_ptr;
    checkpoint.savedExceptionValue = exception_value ? exception_value->copy() : nullptr;
    checkpoint.savedCodePtr = code_ptr;
    checkpoint.savedCodeSize = code_size;
    checkpoint.savedTailEval = tail_eval;
    checkpoint.savedScanDepth = scan_depth;

    checkpoint.listreeValue = Allocator<agentc::ListreeValue>::getAllocator().checkpoint();
    if (!checkpoint.listreeValue.valid) {
        return checkpoint;
    }

    checkpoint.listreeItem = Allocator<agentc::ListreeItem>::getAllocator().checkpoint();
    if (!checkpoint.listreeItem.valid) {
        Allocator<agentc::ListreeValue>::getAllocator().commit(checkpoint.listreeValue);
        return {};
    }

    checkpoint.listreeValueRef = Allocator<agentc::ListreeValueRef>::getAllocator().checkpoint();
    if (!checkpoint.listreeValueRef.valid) {
        Allocator<agentc::ListreeItem>::getAllocator().commit(checkpoint.listreeItem);
        Allocator<agentc::ListreeValue>::getAllocator().commit(checkpoint.listreeValue);
        return {};
    }

    checkpoint.listNode = Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().checkpoint();
    if (!checkpoint.listNode.valid) {
        Allocator<agentc::ListreeValueRef>::getAllocator().commit(checkpoint.listreeValueRef);
        Allocator<agentc::ListreeItem>::getAllocator().commit(checkpoint.listreeItem);
        Allocator<agentc::ListreeValue>::getAllocator().commit(checkpoint.listreeValue);
        return {};
    }

    checkpoint.treeNode = Allocator<AATree<agentc::ListreeItem>>::getAllocator().checkpoint();
    if (!checkpoint.treeNode.valid) {
        Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().commit(checkpoint.listNode);
        Allocator<agentc::ListreeValueRef>::getAllocator().commit(checkpoint.listreeValueRef);
        Allocator<agentc::ListreeItem>::getAllocator().commit(checkpoint.listreeItem);
        Allocator<agentc::ListreeValue>::getAllocator().commit(checkpoint.listreeValue);
        return {};
    }

    checkpoint.blobAllocator = BlobAllocator::getAllocator().checkpoint();
    if (!checkpoint.blobAllocator.valid) {
        Allocator<AATree<agentc::ListreeItem>>::getAllocator().commit(checkpoint.treeNode);
        Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().commit(checkpoint.listNode);
        Allocator<agentc::ListreeValueRef>::getAllocator().commit(checkpoint.listreeValueRef);
        Allocator<agentc::ListreeItem>::getAllocator().commit(checkpoint.listreeItem);
        Allocator<agentc::ListreeValue>::getAllocator().commit(checkpoint.listreeValue);
        return {};
    }

    checkpoint.valid = true;
    return checkpoint;
}

bool EdictVM::rollbackTransaction(EdictVM::TransactionCheckpoint& checkpoint) {
    if (!checkpoint.valid) {
        return false;
    }

    for (size_t i = 0; i < VMRES_COUNT; ++i) {
        if (i == VMRES_CODE && !checkpoint.restoreCodeResource) {
            continue;
        }
        resources[i] = checkpoint.resources[i];
    }
    rewrite_rules.clear();
    rewrite_rules.reserve(checkpoint.rewriteRules.size());
    for (const auto& rule : checkpoint.rewriteRules) {
        rewrite_rules.push_back(RewriteRule{rule.first, rule.second});
    }

    state = checkpoint.savedState;
    instruction_ptr = checkpoint.savedInstructionPtr;
    exception_value = checkpoint.savedExceptionValue;
    code_ptr = checkpoint.savedCodePtr;
    code_size = checkpoint.savedCodeSize;
    tail_eval = checkpoint.savedTailEval;
    scan_depth = checkpoint.savedScanDepth;

    bool ok = true;
    ok = BlobAllocator::getAllocator().rollback(checkpoint.blobAllocator) && ok;
    ok = Allocator<AATree<agentc::ListreeItem>>::getAllocator().rollback(checkpoint.treeNode) && ok;
    ok = Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().rollback(checkpoint.listNode) && ok;
    ok = Allocator<agentc::ListreeValueRef>::getAllocator().rollback(checkpoint.listreeValueRef) && ok;
    ok = Allocator<agentc::ListreeItem>::getAllocator().rollback(checkpoint.listreeItem) && ok;
    ok = Allocator<agentc::ListreeValue>::getAllocator().rollback(checkpoint.listreeValue) && ok;

    if (ok) {
        checkpoint.valid = false;
    }
    return ok;
}

bool EdictVM::commitTransaction(EdictVM::TransactionCheckpoint& checkpoint) {
    if (!checkpoint.valid) {
        return false;
    }

    bool ok = true;
    ok = BlobAllocator::getAllocator().commit(checkpoint.blobAllocator) && ok;
    ok = Allocator<AATree<agentc::ListreeItem>>::getAllocator().commit(checkpoint.treeNode) && ok;
    ok = Allocator<CLL<agentc::ListreeValueRef>>::getAllocator().commit(checkpoint.listNode) && ok;
    ok = Allocator<agentc::ListreeValueRef>::getAllocator().commit(checkpoint.listreeValueRef) && ok;
    ok = Allocator<agentc::ListreeItem>::getAllocator().commit(checkpoint.listreeItem) && ok;
    ok = Allocator<agentc::ListreeValue>::getAllocator().commit(checkpoint.listreeValue) && ok;

    if (ok) {
        checkpoint.valid = false;
    }
    return ok;
}

bool EdictVM::speculate(const std::function<bool(EdictVM&)>& action,
                        CPtr<agentc::ListreeValue>& resultOut,
                        std::string& errorOut) {
    resultOut = nullptr;
    errorOut.clear();
    auto baselineSnapshot = dumpStack();
    SpeculativeSnapshot speculativeSnapshot;

    auto checkpoint = beginTransaction();
    if (!checkpoint.valid) {
        errorOut = "failed to begin speculative transaction";
        return false;
    }

    bool succeeded = action(*this) && (state & VM_ERROR) == 0;

    if (succeeded) {
        auto result = peekData();
        speculativeSnapshot = captureSpeculativeSnapshot(result);
        if (speculativeSnapshot.kind == SpeculativeSnapshot::Kind::None) {
            auto stackSnapshot = dumpStack();
            const size_t baselineCount = listValueCount(baselineSnapshot);
            const size_t stackCount = listValueCount(stackSnapshot);
            auto top = listValueAt(stackSnapshot, 0);
            if (stackCount > baselineCount) {
                speculativeSnapshot.kind = SpeculativeSnapshot::Kind::String;
                if (top && top->getData()) speculativeSnapshot.text.assign(static_cast<char*>(top->getData()), top->getLength());
            } else if (stackCount > 0) {
                speculativeSnapshot.kind = SpeculativeSnapshot::Kind::String;
                if (top && top->getData()) speculativeSnapshot.text.assign(static_cast<char*>(top->getData()), top->getLength());
            }
        }
    } else {
        errorOut = getError();
    }

    if (!rollbackTransaction(checkpoint)) {
        errorOut = errorOut.empty() ? "failed to rollback speculative transaction" : errorOut;
        return false;
    }

    if (succeeded) {
        resultOut = materializeSpeculativeSnapshot(speculativeSnapshot);
    }

    return succeeded;
}

bool EdictVM::speculateValue(const std::function<CPtr<agentc::ListreeValue>(EdictVM&)>& action,
                             CPtr<agentc::ListreeValue>& resultOut,
                             std::string& errorOut) {
    resultOut = nullptr;
    errorOut.clear();

    auto checkpoint = beginTransaction();
    if (!checkpoint.valid) {
        errorOut = "failed to begin speculative transaction";
        return false;
    }

    auto speculativeValue = action(*this);
    auto speculativeSnapshot = captureSpeculativeSnapshot(speculativeValue);
    bool succeeded = (state & VM_ERROR) == 0;
    if (!succeeded) {
        errorOut = getError();
    }

    if (!rollbackTransaction(checkpoint)) {
        errorOut = errorOut.empty() ? "failed to rollback speculative transaction" : errorOut;
        return false;
    }

    if (succeeded) {
        resultOut = materializeSpeculativeSnapshot(speculativeSnapshot);
    }

    return succeeded;
}

bool EdictVM::speculate(const BytecodeBuffer& code,
                        CPtr<agentc::ListreeValue>& resultOut,
                        std::string& errorOut) {
    return speculate([&code](EdictVM& vm) {
        return (vm.execute(code) & VM_ERROR) == 0;
    }, resultOut, errorOut);
}

// Low-level resource management
void EdictVM::enq(VMResource res, CPtr<agentc::ListreeValue> v) {
    if (resources[res]) agentc::addListItem(resources[res], v);
}

CPtr<agentc::ListreeValue> EdictVM::deq(VMResource res, bool pop) {
    return resources[res] ? resources[res]->get(pop, true) : nullptr;
}

CPtr<agentc::ListreeValue> EdictVM::peek(VMResource res) const {
    return const_cast<EdictVM*>(this)->deq(res, false);
}

// High-level stack management (active frame)
void EdictVM::stack_enq(VMResource res, CPtr<agentc::ListreeValue> v) {
    auto frame = peek(res);
    if (frame) agentc::addListItem(frame, v);
}

CPtr<agentc::ListreeValue> EdictVM::stack_deq(VMResource res, bool pop) {
    auto frame = peek(res);
    return frame ? frame->get(pop, true) : nullptr;
}

CPtr<agentc::ListreeValue> EdictVM::getStackTop() const {
    return const_cast<EdictVM*>(this)->peekData();
}

size_t EdictVM::getResourceDepth(VMResource res) const {
    size_t count = 0;
    auto stack = resources[res];
    if (!stack) {
        return 0;
    }

    stack->forEachList([&](CPtr<agentc::ListreeValueRef>&) {
        ++count;
    });
    return count;
}

void EdictVM::initResources(CPtr<agentc::ListreeValue> root) {
    resources[VMRES_DICT] = agentc::createListValue();
    resources[VMRES_STACK] = agentc::createListValue();
    resources[VMRES_FUNC] = agentc::createListValue();
    resources[VMRES_EXCP] = agentc::createListValue();
    resources[VMRES_CODE] = agentc::createListValue();
    resources[VMRES_STATE] = agentc::createListValue();
    code_ptr = nullptr;
    code_size = 0;
    instruction_ptr = 0;
    tail_eval = false;

    // Seed initial frames for data and dictionary stacks
    enq(VMRES_STACK, agentc::createListValue());
    enq(VMRES_DICT, agentc::createListValue());
    stack_enq(VMRES_DICT, root ? root : agentc::createNullValue());
    loadBuiltins();
    runStartupBootstrapPrelude();
}

void EdictVM::resetRuntime() {
    auto code_stack = resources[VMRES_CODE];
    auto saved_ptr = code_ptr;
    auto saved_size = code_size;
    auto saved_ip = instruction_ptr;
    initResources(cursor.getValue());
    resources[VMRES_CODE] = code_stack;
    code_ptr = saved_ptr;
    code_size = saved_size;
    instruction_ptr = saved_ip;
}

// Push a library-definition tree onto the data stack.
// NOTE (G058): Automatic read-only freeze is intentionally disabled here —
// callers commonly mutate the defs immediately after import
// (e.g. normalize_thread_runtime_defs in tests).  When the caller is done
// mutating, they may call defs->setReadOnly(true) themselves before sharing
// the tree across threads.
static void pushFrozenDefinitions(agentc::edict::EdictVM* vm,
                                   CPtr<agentc::ListreeValue> defs) {
    vm->pushData(defs);
}

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

void EdictVM::addCompiledThunk(CPtr<agentc::ListreeValue> dictVal,
                               const std::string& name,
                               const std::string& source) {
    if (!dictVal) return;
    BytecodeBuffer bc = EdictCompiler().compile(source);
    CPtr<agentc::ListreeValue> thunk = makeCodeFrame(bc);
    agentc::addNamedItem(dictVal, name, thunk);
}

CPtr<agentc::ListreeValue> EdictVM::createBootstrapCuratedParser() {
    auto parser = agentc::createNullValue();

    auto native = agentc::createNullValue();
    addBuiltinThunk(native, "map", VMOP_MAP);
    addBuiltinThunk(native, "parse_json", VMOP_PARSE_JSON);
    addBuiltinThunk(native, "materialize_json", VMOP_MATERIALIZE_JSON);
    agentc::addNamedItem(parser, "__native", native);

    addCompiledThunk(parser, "map",
                     "@header "
                     "header parser.parse_json ! @schema "
                     "schema parser.materialize_json !");
    addCompiledThunk(parser, "parse_json", "parser.__native.parse_json !");
    addCompiledThunk(parser, "materialize_json", "parser.__native.materialize_json !");
    addBootstrapMetadata(parser, "parser");
    return parser;
}

CPtr<agentc::ListreeValue> EdictVM::createBootstrapCuratedResolver() {
    auto resolver = agentc::createNullValue();

    auto native = agentc::createNullValue();
    addBuiltinThunk(native, "load", VMOP_LOAD);
    addBuiltinThunk(native, "import", VMOP_IMPORT);
    addBuiltinThunk(native, "import_resolved", VMOP_IMPORT_RESOLVED);
    addBuiltinThunk(native, "resolve_json", VMOP_RESOLVE_JSON);
    addBuiltinThunk(native, "import_resolved_json", VMOP_IMPORT_RESOLVED_JSON);
    addBuiltinThunk(native, "read_text", VMOP_READ_TEXT);
    addBuiltinThunk(native, "request_id", VMOP_REQUEST_ID);
    addBuiltinThunk(native, "import_deferred", VMOP_IMPORT_DEFERRED);
    addBuiltinThunk(native, "import_collect", VMOP_IMPORT_COLLECT);
    addBuiltinThunk(native, "import_status", VMOP_IMPORT_STATUS);
    agentc::addNamedItem(resolver, "__native", native);

    addCompiledThunk(resolver, "load", "resolver.__native.load !");
    addCompiledThunk(resolver, "resolve_json", "resolver.__native.resolve_json !");
    addCompiledThunk(resolver, "import_resolved_json", "resolver.__native.import_resolved_json !");
    addCompiledThunk(resolver, "import", "resolver.__native.import !");
    addCompiledThunk(resolver, "import_resolved",
                     "@resolved_path "
                     "resolved_path resolver.__native.read_text ! @resolved "
                     "resolved resolver.import_resolved_json !");
    addCompiledThunk(resolver, "import_deferred", "resolver.__native.import_deferred !");
    addCompiledThunk(resolver, "import_collect",
                     "@request "
                     "request resolver.__native.request_id ! @request_id "
                     "request_id resolver.__native.import_collect !");
    addCompiledThunk(resolver, "import_status",
                     "@request "
                     "request resolver.__native.request_id ! @request_id "
                     "request_id resolver.__native.import_status !");
    addBootstrapMetadata(resolver, "resolver");
    return resolver;
}

void EdictVM::op_BOOTSTRAP_CURATE_PARSER() {
    pushData(createBootstrapCuratedParser());
}

void EdictVM::op_BOOTSTRAP_CURATE_RESOLVER() {
    pushData(createBootstrapCuratedResolver());
}

// Build a function-def LTV tree suitable for FFI dispatch, with all parameters
// and the return value typed as "ltv" (the SlabId passthrough sentinel).
// paramNames lists the parameter names in call order.
// If hasReturnValue is false, return_type is "void".
static CPtr<agentc::ListreeValue> buildBoxingFuncDef(
    const std::string& funcName,
    const std::vector<std::string>& paramNames,
    bool hasReturnValue)
{
    auto def = agentc::createNullValue();
    agentc::addNamedItem(def, "kind",        agentc::createStringValue("Function"));
    agentc::addNamedItem(def, "name",        agentc::createStringValue(funcName));
    agentc::addNamedItem(def, "return_type", agentc::createStringValue(hasReturnValue ? "ltv" : "void"));

    auto children = agentc::createNullValue();
    for (const auto& pname : paramNames) {
        auto param = agentc::createNullValue();
        agentc::addNamedItem(param, "kind", agentc::createStringValue("Parameter"));
        agentc::addNamedItem(param, "name", agentc::createStringValue(pname));
        agentc::addNamedItem(param, "type", agentc::createStringValue("ltv"));
        agentc::addNamedItem(children, pname, param);
    }
    agentc::addNamedItem(def, "children", children);
    return def;
}

CPtr<agentc::ListreeValue> EdictVM::createBootstrapCuratedCartographer() {
    auto cartographerNs = agentc::createNullValue();

    // Phase D: boxing operations are now pure FFI calls — no VM opcodes needed.
    // Prime the FFI handle with the process symbol table so that agentc_box,
    // agentc_unbox, and agentc_box_free (compiled into libcartographer.so,
    // a build-time dependency) are visible to dlsym without a runtime path.
    ffi->loadProcessSymbols();

    // Build function-def trees with "ltv" type annotations.
    // Stack convention:
    //   cartographer.box !      — ( source_ltv type_def -- boxed )
    //   cartographer.unbox !    — ( boxed -- unboxed_ltv )
    //   cartographer.box_free ! — ( boxed -- )
    auto boxDef      = buildBoxingFuncDef("agentc_box",      {"source", "type_def"}, true);
    auto unboxDef    = buildBoxingFuncDef("agentc_unbox",    {"boxed"},              true);
    auto boxFreeDef  = buildBoxingFuncDef("agentc_box_free", {"boxed"},              false);

    agentc::addNamedItem(cartographerNs, "box",      boxDef);
    agentc::addNamedItem(cartographerNs, "unbox",    unboxDef);
    agentc::addNamedItem(cartographerNs, "box_free", boxFreeDef);

    addBootstrapMetadata(cartographerNs, "cartographer");
    return cartographerNs;
}

void EdictVM::op_BOOTSTRAP_CURATE_CARTOGRAPHER() {
    pushData(createBootstrapCuratedCartographer());
}

bool EdictVM::enforceImportedFunctionPolicy(const std::string& funcName, CPtr<agentc::ListreeValue> funcDef) {
    if (!funcDef) {
        return true;
    }

    if (stringField(funcDef, "imported_via") != "cartographer_service") {
        return true;
    }

    if (stringField(funcDef, "safety") == "unsafe" && !allow_unsafe_ffi_calls) {
        setError("Cartographer blocked unsafe import: " + funcName);
        return false;
    }

    return true;
}

void EdictVM::addRewriteRule(const std::vector<std::string>& pattern, const std::vector<std::string>& replacement) {
    rewrite_rules.push_back(RewriteRule{pattern, replacement});
}

size_t EdictVM::getRewriteRuleCount() const {
    return rewrite_rules.size();
}

std::vector<std::string> EdictVM::getRewriteRulePattern(size_t index) const {
    if (index >= rewrite_rules.size()) {
        return {};
    }
    return rewrite_rules[index].pattern;
}

std::vector<std::string> EdictVM::getRewriteRuleReplacement(size_t index) const {
    if (index >= rewrite_rules.size()) {
        return {};
    }
    return rewrite_rules[index].replacement;
}

bool EdictVM::removeRewriteRule(size_t index) {
    if (index >= rewrite_rules.size()) {
        return false;
    }
    rewrite_rules.erase(rewrite_rules.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

void EdictVM::op_REWRITE_APPLY() {
    applyRewriteLoop(true);
    pushData(last_rewrite_trace ? last_rewrite_trace->copy() : agentc::createNullValue());
}

void EdictVM::op_REWRITE_MODE() {
    auto modeValue = popData();
    RewriteMode parsed = rewrite_mode;
    if (!parseRewriteMode(modeValue, parsed)) {
        setError("rewrite_mode expects one of: auto, manual, off");
        return;
    }
    rewrite_mode = parsed;
    pushData(agentc::createStringValue(rewriteModeName(rewrite_mode)));
}

void EdictVM::op_REWRITE_TRACE() {
    pushData(last_rewrite_trace ? last_rewrite_trace->copy() : agentc::createNullValue());
}

void EdictVM::op_SPECULATE() {
    auto codeValue = popData();
    std::string source;
    if (!valueToString(codeValue, source)) {
        pushData(agentc::createNullValue());
        return;
    }

    BytecodeBuffer speculativeCode;
    try {
        speculativeCode = EdictCompiler().compile(source);
    } catch (...) {
        pushData(agentc::createNullValue());
        return;
    }

    auto checkpoint = beginTransaction(false);
    if (!checkpoint.valid) {
        pushData(agentc::createNullValue());
        return;
    }

    SpeculativeSnapshot snapshot;
    bool succeeded = false;
    if (!(executeNested(speculativeCode) & VM_ERROR)) {
        snapshot = captureSpeculativeSnapshot(peekData());
        succeeded = true;
    }

    rollbackTransaction(checkpoint);
    if (!succeeded) {
        pushData(agentc::createNullValue());
        return;
    }

    pushData(materializeSpeculativeSnapshot(snapshot));
}

void EdictVM::op_UNSAFE_EXTENSIONS_ALLOW() {
    allow_unsafe_ffi_calls = true;
    pushData(agentc::createStringValue(unsafeExtensionsStatusName(allow_unsafe_ffi_calls)));
}

void EdictVM::op_UNSAFE_EXTENSIONS_BLOCK() {
    allow_unsafe_ffi_calls = false;
    pushData(agentc::createStringValue(unsafeExtensionsStatusName(allow_unsafe_ffi_calls)));
}

void EdictVM::op_UNSAFE_EXTENSIONS_STATUS() {
    pushData(agentc::createStringValue(unsafeExtensionsStatusName(allow_unsafe_ffi_calls)));
}

void EdictVM::op_REWRITE_DEFINE() {
    auto spec = popData();
    if (!spec || spec->isListMode()) {
        setError("REWRITE_DEFINE expects rule object");
        return;
    }

    std::vector<std::string> pattern;
    std::vector<std::string> replacement;
    std::string error;

    if (!stringListFromValue(namedValue(spec, "pattern"), pattern, error, "pattern")) {
        setError(error);
        return;
    }

    if (!stringListFromValue(namedValue(spec, "replacement"), replacement, error, "replacement")) {
        setError(error);
        return;
    }

    if (pattern.empty()) {
        setError("pattern must not be empty");
        return;
    }

    addRewriteRule(pattern, replacement);
    pushData(spec);
}

void EdictVM::op_REWRITE_LIST() {
    auto out = agentc::createListValue();
    for (size_t i = 0; i < rewrite_rules.size(); ++i) {
        const auto& rule = rewrite_rules[i];
        agentc::addListItem(out, createRewriteRuleValue(i, rule.pattern, rule.replacement));
    }
    pushData(out);
}

void EdictVM::op_REWRITE_REMOVE() {
    auto value = popData();
    size_t index = 0;
    if (!parseRewriteIndex(value, index)) {
        setError("REWRITE_REMOVE expects numeric string index");
        return;
    }
    if (index >= rewrite_rules.size()) {
        setError("rewrite rule index out of range");
        return;
    }

    const auto removed = rewrite_rules[index];
    rewrite_rules.erase(rewrite_rules.begin() + static_cast<std::ptrdiff_t>(index));
    pushData(createRewriteRuleValue(index, removed.pattern, removed.replacement));
}

CPtr<agentc::ListreeValue> EdictVM::makeCodeFrame(const BytecodeBuffer& code) {
    const auto& data = code.getData();
    if (data.empty()) return agentc::createBinaryValue(nullptr, 0);
    void* buf = std::malloc(data.size());
    if (!buf) return nullptr;
    std::memcpy(buf, data.data(), data.size());
    CPtr<agentc::ListreeValue> frame = agentc::createBinaryValue(buf, data.size());
    writeFrameIp(frame, 0);
    return frame;
}

void EdictVM::pushCodeFrame(const BytecodeBuffer& code) {
    auto frame = makeCodeFrame(code);
    if (frame) enq(VMRES_CODE, frame);
}

void EdictVM::popCodeFrame() {
    deq(VMRES_CODE, true);
}

CPtr<agentc::ListreeValue> EdictVM::peekCodeFrame() {
    return peek(VMRES_CODE);
}

void EdictVM::op_RESET() {
    // Reset VM state logic if needed, e.g. clear stacks or reset extension register equivalent
    // In J3, we might not have an explicit 'ext' register to clear if we push directly to stack.
    // But if we want to support 'RESET', we should define it.
    // For now, no-op or clear error.
    error_message.clear();
    state &= ~VM_ERROR;
}

void EdictVM::writeFrameIp(CPtr<agentc::ListreeValue> frame, int ip) {
    if (!frame) return;
    auto item = frame->find(".ip", true);
    if (!item) return;
    auto v = item->getValue(false, false);
    if (v && (v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None &&
        v->getLength() == sizeof(int)) {
        // In-place update: the ip is packed into the data pointer field itself.
        // (void*)(uintptr_t)ip was stored without any heap allocation.
        // Overwrite by replacing the value with a fresh slab-resident int value.
        item->getValue(true, false); // pop the stale value
    }
    // Store IP as a pointer-packed integer: data = (void*)(uintptr_t)ip,
    // length = sizeof(int), LtvFlags::Binary (no Free, no Duplicate).
    // No heap allocation — the integer is encoded in the pointer field itself,
    // which lives inline in the slab node. Slab rollback is safe: no destructor
    // side-effect because LtvFlags::Free is absent.
    void* packed = reinterpret_cast<void*>(static_cast<uintptr_t>(static_cast<unsigned int>(ip)));
    SlabId sid = Allocator<agentc::ListreeValue>::getAllocator().allocate(
        packed, sizeof(int), agentc::LtvFlags::Binary);
    CPtr<agentc::ListreeValue> ipVal(sid);
    item->addValue(ipVal, false);
}

int EdictVM::readFrameIp(CPtr<agentc::ListreeValue> frame) const {
    if (!frame) return 0;
    auto item = frame->find(".ip");
    if (!item) return 0;
    auto v = item->getValue(false, false);
    if (!v || v->getLength() != sizeof(int) ||
        (v->getFlags() & agentc::LtvFlags::Binary) == agentc::LtvFlags::None) return 0;
    // Recover the IP packed into the data pointer field.
    return static_cast<int>(static_cast<unsigned int>(
        reinterpret_cast<uintptr_t>(v->getData())));
}

size_t EdictVM::getStackSize() const {
    size_t count = 0;
    auto frame = peek(VMRES_STACK);
    if (frame) {
        frame->forEachList([&](CPtr<agentc::ListreeValueRef>&){ count++; });
    }
    return count;
}

    // Listree concatenation helper
    void EdictVM::listcat(VMResource res) {
        auto tos = deq(res, true);
        auto nos = peek(res);
        if (tos && nos) {
            if (edictTraceEnabled()) {
                std::cout << "Listcat merging " << tos->getLength() << " items" << std::endl;
            }
            tos->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
                if (ref && ref->getValue()) agentc::addListItem(nos, ref->getValue());
            });
        }
    }

CPtr<agentc::ListreeValue> EdictVM::dumpStack() const {
    auto items = agentc::createListValue();
    auto frame = peek(VMRES_STACK);
    if (!frame) return items;
    frame->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        CPtr<agentc::ListreeValue> v = ref ? ref->getValue() : nullptr;
        agentc::addListItem(items, agentc::createStringValue(formatValueForDisplay(v)));
    });
    return items;
}

bool EdictVM::applyRewriteOnce(bool manualTrigger) {
    if (rewrite_mode == RewriteMode::Off) {
        last_rewrite_trace = createRewriteTraceValue("skipped", "rewrite mode is off", rewriteModeName(rewrite_mode));
        return false;
    }
    if (!manualTrigger && rewrite_mode == RewriteMode::Manual) {
        last_rewrite_trace = createRewriteTraceValue("skipped", "rewrite mode is manual", rewriteModeName(rewrite_mode));
        return false;
    }
    if (rewrite_rules.empty()) {
        last_rewrite_trace = createRewriteTraceValue("no_match", "no rewrite rules registered", rewriteModeName(rewrite_mode));
        return false;
    }
    auto frame = peek(VMRES_STACK);
    if (!frame) {
        last_rewrite_trace = createRewriteTraceValue("no_match", "no active stack frame", rewriteModeName(rewrite_mode));
        return false;
    }

    size_t stack_size = 0;
    frame->forEachList([&](CPtr<agentc::ListreeValueRef>&){ stack_size++; });
    if (stack_size == 0) {
        last_rewrite_trace = createRewriteTraceValue("no_match", "active stack is empty", rewriteModeName(rewrite_mode));
        return false;
    }

    size_t max_len = 0;
    for (const auto& rule : rewrite_rules) {
        max_len = std::max(max_len, rule.pattern.size());
    }
    if (max_len == 0) {
        last_rewrite_trace = createRewriteTraceValue("no_match", "rewrite rules have empty patterns", rewriteModeName(rewrite_mode));
        return false;
    }

    const size_t pop_count = std::min(stack_size, max_len);
    auto popped = agentc::createListValue();
    for (size_t i = 0; i < pop_count; ++i) {
        auto v = popData();
        if (!v) break;
        agentc::addListItem(popped, v); // TOS first in logical order
    }
    const size_t poppedCount = listValueCount(popped);
    if (poppedCount == 0) {
        last_rewrite_trace = createRewriteTraceValue("no_match", "unable to inspect stack values", rewriteModeName(rewrite_mode));
        return false;
    }

    const RewriteRule* best_rule = nullptr;
    size_t best_len = 0;
    size_t best_index = 0;
    std::unordered_map<std::string, CPtr<agentc::ListreeValue>> best_captures;
    std::string best_failure_reason = "no rule matched the current stack suffix";
    for (size_t ruleIndex = 0; ruleIndex < rewrite_rules.size(); ++ruleIndex) {
        const auto& rule = rewrite_rules[ruleIndex];
        const size_t pat_len = rule.pattern.size();
        if (pat_len == 0 || pat_len > poppedCount) continue;

        bool match = true;
        std::unordered_map<std::string, CPtr<agentc::ListreeValue>> captures;
        std::string failure_reason;
        for (size_t i = 0; i < pat_len; ++i) {
            const std::string& pattern_token = rule.pattern[pat_len - 1 - i];
            auto poppedValue = listValueAt(popped, i);
            if (!poppedValue) {
                match = false;
                failure_reason = "rule " + std::to_string(ruleIndex) + " could not inspect stack value";
                break;
            }
            if (isRewriteWildcard(pattern_token)) {
                auto it = captures.find(pattern_token);
                if (it == captures.end()) {
                    captures.emplace(pattern_token, poppedValue);
                    continue;
                }

                if (it->second != poppedValue) {
                    std::string lhs;
                    std::string rhs;
                    if (!valueToString(it->second, lhs) || !valueToString(poppedValue, rhs) || lhs != rhs) {
                        match = false;
                        failure_reason = "rule " + std::to_string(ruleIndex) + " wildcard " + pattern_token + " captured conflicting values";
                        break;
                    }
                }
                continue;
            }

            if (isRewriteTypeToken(pattern_token)) {
                if (!rewriteTypeMatches(pattern_token, poppedValue)) {
                    match = false;
                    failure_reason = "rule " + std::to_string(ruleIndex) + " expected " + pattern_token + " but saw " + formatValueForDisplay(poppedValue);
                    break;
                }
                continue;
            }

            std::string val;
            if (!valueToString(poppedValue, val) || val != pattern_token) {
                match = false;
                failure_reason = "rule " + std::to_string(ruleIndex) + " expected '" + pattern_token + "' but saw " + formatValueForDisplay(poppedValue);
                break;
            }
        }
        if (match && pat_len > best_len) {
            best_len = pat_len;
            best_rule = &rule;
            best_index = ruleIndex;
            best_captures = std::move(captures);
        } else if (!match && best_failure_reason == "no rule matched the current stack suffix" && !failure_reason.empty()) {
            best_failure_reason = failure_reason;
        }
    }

    if (!best_rule) {
        for (size_t i = poppedCount; i-- > 0;) {
            auto poppedValue = listValueAt(popped, i);
            if (poppedValue) {
                pushData(poppedValue);
            }
        }
        last_rewrite_trace = createRewriteTraceValue("no_match", best_failure_reason, rewriteModeName(rewrite_mode));
        return false;
    }

    for (size_t i = poppedCount; i-- > best_len;) {
        auto poppedValue = listValueAt(popped, i);
        if (poppedValue) {
            pushData(poppedValue);
        }
    }
    for (const auto& token : best_rule->replacement) {
        if (isRewriteWildcard(token)) {
            auto it = best_captures.find(token);
            if (it != best_captures.end()) {
                pushData(it->second);
                continue;
            }
        }

        pushData(agentc::createStringValue(token));
    }
    last_rewrite_trace = createRewriteTraceValue(
        "matched",
        manualTrigger ? "rewrite applied by manual trigger" : "rewrite applied during automatic epilogue",
        rewriteModeName(rewrite_mode),
        best_index,
        &best_rule->pattern,
        &best_rule->replacement);
    return true;
}

void EdictVM::applyRewriteLoop(bool manualTrigger) {
    if (manualTrigger) {
        applyRewriteOnce(true);
        return;
    }

    const size_t max_steps = 16;
    CPtr<agentc::ListreeValue> lastMatchedTrace = nullptr;
    for (size_t i = 0; i < max_steps; ++i) {
        if (!applyRewriteOnce(manualTrigger)) {
            if (lastMatchedTrace) {
                last_rewrite_trace = lastMatchedTrace;
            }
            return;
        }
        lastMatchedTrace = last_rewrite_trace ? last_rewrite_trace->copy() : nullptr;
    }
}

uint8_t EdictVM::readByte() { if (!code_ptr || instruction_ptr >= code_size) { return 0; } return code_ptr[instruction_ptr++]; }
int EdictVM::readInt() { int val = 0; for (size_t i = 0; i < sizeof(int); i++) val |= (static_cast<int>(readByte()) << (i * 8)); return val; }
std::string EdictVM::readString() { int len = readInt(); std::string s; if (len > 0) { s.reserve(len); for (int i = 0; i < len; i++) s.push_back((char)readByte()); } return s; }
CPtr<agentc::ListreeValue> EdictVM::readValue() {
    uint8_t type = readByte(); if (state & VM_ERROR) return nullptr;
    switch (type) {
        case VMEXT_NULL:   return agentc::createNullValue();
        case VMEXT_BOOL:   { uint8_t b = readByte(); return agentc::createBinaryValue(&b, 1); }
        case VMEXT_STRING: return agentc::createStringValue(readString());
        case VMEXT_DICT:   readString(); return agentc::createNullValue();
        case VMEXT_LIST:   return agentc::createListValue();
        default: setError("Type error"); return nullptr;
    }
}

void EdictVM::op_YIELD() { state |= VM_YIELD; }
void EdictVM::op_PUSHEXT() { 
    auto v = readValue(); 
    if (v) pushData(v); 
}

void EdictVM::op_SPLICE() {
    auto destNode = popData();
    if (!destNode) { setError("Splicing (^) requires a destination node on the stack"); return; }
    
    // Auto-resolve symbol if string
    if (destNode->getData() && (destNode->getFlags() & agentc::LtvFlags::Binary) == agentc::LtvFlags::None && !destNode->isListMode()) {
        std::string k(static_cast<char*>(destNode->getData()), destNode->getLength());
        auto dictStack = resources[VMRES_DICT];
        if (dictStack) {
            CPtr<agentc::ListreeValue> found = nullptr;
            bool done = false;
            dictStack->forEachList([&](CPtr<agentc::ListreeValueRef>& frameRef) {
                if (done || !frameRef || !frameRef->getValue()) return;
                auto frame = frameRef->getValue();
                // std::cout << "SPLICE searching frame" << std::endl;
                frame->forEachList([&](CPtr<agentc::ListreeValueRef>& scopeRef) {
                    if (done || !scopeRef || !scopeRef->getValue()) return;
                    agentc::Cursor ctx(scopeRef->getValue());
                    if (edictTraceEnabled()) {
                        std::cout << "SPLICE searching scope for " << k << std::endl;
                    }
                    if (ctx.resolve(k)) { 
                        if (edictTraceEnabled()) {
                            std::cout << "SPLICE found " << k << std::endl;
                        }
                        found = ctx.getValue(); 
                        done = true; 
                    } else if (!ctx.getLastError().empty()) {
                        enq(VMRES_STATE, agentc::createStringValue(ctx.getLastError()));
                        done = true;
                    }
                }, true);
            }, true);
            if (done && !found) return;
            if (found) destNode = found;
        }
    }

    auto activeFrame = peek(VMRES_STACK);
    if (activeFrame) {
        if (edictTraceEnabled()) {
            std::cout << "SPLICE: ActiveFrame size " << getStackSize() << std::endl;
        }
        
        // Ensure destNode is in list mode and not null
        destNode->setFlags(agentc::LtvFlags::List);
        destNode->clearFlags(agentc::LtvFlags::Null);

        // Move items from activeFrame to destNode
        auto items = copyListValue(activeFrame);
        const size_t itemCount = listValueCount(items);
        items->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
            if (ref && ref->getValue() && edictTraceEnabled()) {
                std::cout << "SPLICE: Moving item " << formatValueForDisplay(ref->getValue()) << std::endl;
            }
        });
        
        if (edictTraceEnabled()) {
            std::cout << "SPLICE: Collected " << itemCount << " items" << std::endl;
        }

        // Clear active frame
        while(activeFrame->get(true, true)); 
        
        // Add items to destNode
        items->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
            if (ref && ref->getValue()) {
                destNode->put(ref->getValue());
            }
        }, false);
        
        if (edictTraceEnabled()) {
            std::cout << "SPLICE: destNode now has " << destNode->getLength() << " bytes and list=" << destNode->isListMode() << std::endl;
        }

        // Push destNode back onto activeFrame
        activeFrame->put(destNode);
        if (edictTraceEnabled()) {
            std::cout << "SPLICE: activeFrame now has " << getStackSize() << " items" << std::endl;
        }
    } else {
        // Should not happen if CTX_PUSH was called, but if at root:
        pushData(destNode);
    }
    // Note: We don't push destNode to the DATA stack here because it IS the frame now.
    // Wait, if it IS the frame, pushes go into it.
    // When CTX_POP happens, it pops the frame (destNode) and pushes it to previous stack.
    // So correct.
}
void EdictVM::op_DUP() { pushData(peekData()); }
void EdictVM::op_SWAP() { 
    auto a = popData(); auto b = popData(); 
    if (a && b) { pushData(a); pushData(b); } 
    else setError("Underflow"); 
}
void EdictVM::op_POP() { popData(); }

void EdictVM::op_REF() { 

    auto kVal = popData(); if (!kVal) return; 

    if (!kVal->getData() || (kVal->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {

        pushData(kVal);

        return;

    }

    std::string k(static_cast<char*>(kVal->getData()), kVal->getLength());

    

    auto dictStack = resources[VMRES_DICT];

    if (!dictStack) { pushData(kVal); return; }

    

    CPtr<agentc::ListreeValue> found = nullptr;

    bool done = false;

    

    // Iterate through dictionary frames (Newest first)

    dictStack->forEachList([&](CPtr<agentc::ListreeValueRef>& frameRef) {

        if (done || !frameRef || !frameRef->getValue()) return;

        auto frame = frameRef->getValue();

        // Iterate through dictionaries in this frame (Newest first)

        frame->forEachList([&](CPtr<agentc::ListreeValueRef>& scopeRef) {

            if (done || !scopeRef || !scopeRef->getValue()) return;

            agentc::Cursor ctx(scopeRef->getValue());

            if (ctx.resolve(k)) { 

                found = ctx.getValue(); 

                done = true; 

            } else if (!ctx.getLastError().empty()) {

                enq(VMRES_STATE, agentc::createStringValue(ctx.getLastError()));

                done = true; 

            }

        }, true);

    }, true);

    

    if (done && !found) {
        return;
    }

    if (found) {
        // std::cout << "REF found: " << k << std::endl;
        pushData(found);
    } else {
        if (edictTraceEnabled()) {
            std::cout << "REF NOT found: " << k << std::endl;
        }
        // Fallback: push original key
        pushData(kVal);
    }
}

void EdictVM::op_ASSIGN() { 
    auto kVal = popData();
    auto v = popData();
    if (!v || !kVal) return; 
    if (!kVal->getData() || (kVal->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return;
    std::string k(static_cast<char*>(kVal->getData()), kVal->getLength()); 
    auto dictFrame = peek(VMRES_DICT);
    auto ctxVal = dictFrame ? dictFrame->get(false, true) : nullptr;
    if (!ctxVal) return; 

    if (!ctxVal->isListMode() && k.find('.') == std::string::npos) {
        agentc::addNamedItem(ctxVal, k, v);
        return;
    }

    agentc::Cursor ctx(ctxVal); 
    if (ctx.resolve(k, true)) {
        // std::cout << "ASSIGN: Updated " << k << std::endl;
        ctx.assign(v);
    } else if (!ctx.getLastError().empty()) {
        enq(VMRES_STATE, agentc::createStringValue(ctx.getLastError()));
    } else {
        ctx.create(k, v); 
    }
}

void EdictVM::op_REMOVE() {
    auto kVal = popData(); if (!kVal || !kVal->getData()) return;
    if ((kVal->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return;
    std::string k(static_cast<char*>(kVal->getData()), kVal->getLength());
    auto dictFrame = peek(VMRES_DICT);
    if (!dictFrame) return;
    bool removed = false;
    dictFrame->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (removed || !ref || !ref->getValue()) return;
        agentc::Cursor ctx(ref->getValue());
        if (ctx.resolve(k)) { ctx.remove(); removed = true; }
        else if (!ctx.getLastError().empty()) {
            enq(VMRES_STATE, agentc::createStringValue(ctx.getLastError()));
            removed = true;
        }
    }, true);
}

void EdictVM::executeIterativeFFI(const std::string& funcName, CPtr<agentc::ListreeValue> funcDef, 
                                  CPtr<agentc::ListreeValue> args,
                                  size_t index,
                                  CPtr<agentc::ListreeValue> builtArgs,
                                  CPtr<agentc::ListreeValue>& resultList) {
    const size_t argCount = listValueCount(args);
    if (index >= argCount) {
         auto invokeArgs = agentc::createListValue();
         builtArgs->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
             if (ref && ref->getValue()) agentc::addListItem(invokeArgs, ref->getValue());
         }, false);
         auto res = ffi->invoke(funcName, funcDef, invokeArgs);
         if (res) agentc::addListItem(resultList, res);
         else agentc::addListItem(resultList, agentc::createNullValue());
         return;
    }
    
    auto arg = listValueAt(args, index);
    if (arg && (arg->getFlags() & agentc::LtvFlags::Iterator) != agentc::LtvFlags::None) {
        agentc::Cursor* c = static_cast<agentc::Cursor*>(arg->getData());
        if (!c) return;
        
        // Clone cursor to ensure we can iterate multiple times if needed (Cartesian Product)
        agentc::Cursor iter = c->clone();
        
        // Collect values first to avoid state issues during recursion
        auto values = agentc::createListValue();
        if (iter.isValid()) {
             CPtr<agentc::ListreeValue> val = iter.getValue();
             if (val) agentc::addListItem(values, val);
        }
        while (iter.next()) {
             CPtr<agentc::ListreeValue> val = iter.getValue();
             if (val) agentc::addListItem(values, val);
        }

        const size_t valueCount = listValueCount(values);
        for (size_t i = 0; i < valueCount; ++i) {
            auto v = listValueAt(values, i);
            if (!v) continue;
            agentc::addListItem(builtArgs, v);
            executeIterativeFFI(funcName, funcDef, args, index + 1, builtArgs, resultList);
            builtArgs->get(true, true);
        }
    } else {
        agentc::addListItem(builtArgs, arg);
        executeIterativeFFI(funcName, funcDef, args, index + 1, builtArgs, resultList);
        builtArgs->get(true, true);
    }
}

// ---------------------------------------------------------------------------
// op_EVAL phase helpers
// Each helper returns true if it consumed the value and the EVAL is done.
// ---------------------------------------------------------------------------

// Phase 1: Iterator / Superposition fan-out.
// If v is an iterator cursor, expand it into a series of EVAL sub-invocations.
bool EdictVM::evalDispatchIterator(CPtr<agentc::ListreeValue> v) {
    if ((v->getFlags() & agentc::LtvFlags::Iterator) == agentc::LtvFlags::None) return false;

    agentc::Cursor* c = static_cast<agentc::Cursor*>(v->getData());
    if (!c) {
        tail_eval = false;
        writeFrameIp(peek(VMRES_CODE), static_cast<int>(instruction_ptr));
        return true;
    }

    agentc::Cursor iter = c->clone();
    auto funcs = agentc::createListValue();
    if (iter.isValid()) {
        CPtr<agentc::ListreeValue> val = iter.getValue();
        if (val) agentc::addListItem(funcs, val);
    }
    while (iter.next()) {
        CPtr<agentc::ListreeValue> val = iter.getValue();
        if (val) agentc::addListItem(funcs, val);
    }

    const size_t funcCount = listValueCount(funcs);
    for (size_t i = funcCount; i-- > 0;) {
        auto func = listValueAt(funcs, i);
        if (!func) continue;
        pushData(func);
        BytecodeBuffer bc; bc.addOp(VMOP_EVAL);
        pushCodeFrame(bc);
    }

    if (tail_eval) {
        popCodeFrame();
        tail_eval = false;
    }
    writeFrameIp(peek(VMRES_CODE), static_cast<int>(instruction_ptr));
    return true;
}

// Phase 2: Binary thunk dispatch.
// Binary Listree nodes that carry a ".ip" field are compiled code frames.
bool EdictVM::evalDispatchThunk(CPtr<agentc::ListreeValue> v) {
    if ((v->getFlags() & agentc::LtvFlags::Binary) == agentc::LtvFlags::None) return false;

    auto ipItem = v->find(".ip");
    if (ipItem) {
        writeFrameIp(v, 0);
        if (tail_eval) {
            popCodeFrame();
            tail_eval = false;
        }
        enq(VMRES_CODE, v);
        return true;
    }
    tail_eval = false;
    writeFrameIp(peek(VMRES_CODE), static_cast<int>(instruction_ptr));
    return true;
}

// Phase 3: FFI function dispatch.
// Dictionary nodes with kind="Function" are FFI-imported C functions.
bool EdictVM::evalDispatchFFI(CPtr<agentc::ListreeValue> v) {
    if (v->isListMode()) return false;

    auto kindItem = v->find("kind");
    if (!kindItem) return false;

    auto kVal = kindItem->getValue(false, false);
    std::string kindStr;
    if (!valueToString(kVal, kindStr) || kindStr != "Function") return false;

    auto nameItem = v->find("name");
    std::string funcName;
    if (!nameItem || !valueToString(nameItem->getValue(false, false), funcName)) return false;

    if (!enforceImportedFunctionPolicy(funcName, v)) return true;

    // Collect parameter nodes.
    auto paramNodes = agentc::createListValue();
    auto childrenItem = v->find("children");
    if (childrenItem) {
        auto childrenVal = childrenItem->getValue(false, false);
        if (childrenVal) {
            childrenVal->forEachTree([&](const std::string&, CPtr<agentc::ListreeItem>& item){
                auto node = item->getValue();
                if (node) agentc::addListItem(paramNodes, node);
            });
        }
    }
    const size_t paramCount = listValueCount(paramNodes);

    // Collect arguments from stack.
    auto args = agentc::createListValue();
    for (size_t i = 0; i < paramCount; ++i) {
        auto arg = popData();
        if (arg) {
            args->put(arg, false);
        } else {
            setError("Stack underflow for FFI call");
            return true;
        }
    }

    // Auto-create callback closures when a parameter defines a signature.
    auto convertedArgs = agentc::createListValue();
    const size_t argCount = listValueCount(args);
    for (size_t i = 0; i < argCount && i < paramCount; ++i) {
        auto paramNode = listValueAt(paramNodes, i);
        auto arg = listValueAt(args, i);
        if (!paramNode) {
            if (arg) agentc::addListItem(convertedArgs, arg);
            continue;
        }

        auto signatureItem = paramNode->find("signature");
        if (!signatureItem) signatureItem = paramNode->find("callback_signature");
        if (!signatureItem) {
            if (arg) agentc::addListItem(convertedArgs, arg);
            continue;
        }
        auto signature = signatureItem->getValue(false, false);
        if (!signature) {
            if (arg) agentc::addListItem(convertedArgs, arg);
            continue;
        }

        auto typeItem = paramNode->find("type");
        std::string typeStr;
        if (!typeItem || !valueToString(typeItem->getValue(false, false), typeStr) || typeStr != "pointer") {
            if (arg) agentc::addListItem(convertedArgs, arg);
            continue;
        }

        if (arg && (arg->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None
            && arg->getLength() == sizeof(void*)) {
            agentc::addListItem(convertedArgs, arg);
            continue;
        }

        auto closureValue = buildClosureValue(signature, arg);
        if (!closureValue) { setError("Failed to create FFI closure from signature"); return true; }
        agentc::addListItem(convertedArgs, closureValue);
    }
    for (size_t i = paramCount; i < argCount; ++i) {
        auto arg = listValueAt(args, i);
        if (arg) agentc::addListItem(convertedArgs, arg);
    }
    args = convertedArgs;

    // Invoke FFI with Superposition Support.
    bool hasCursor = false;
    args->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (hasCursor || !ref || !ref->getValue()) return;
        auto currentArg = ref->getValue();
        if (currentArg && (currentArg->getFlags() & agentc::LtvFlags::Iterator) != agentc::LtvFlags::None) {
            hasCursor = true;
        }
    }, false);

    if (hasCursor) {
        auto builtArgs = agentc::createListValue();
        CPtr<agentc::ListreeValue> resultList = agentc::createListValue();
        executeIterativeFFI(funcName, v, args, 0, builtArgs, resultList);
        pushData(resultList);
    } else {
        auto invokeArgs = agentc::createListValue();
        args->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
            if (ref && ref->getValue()) agentc::addListItem(invokeArgs, ref->getValue());
        }, false);
        auto res = ffi->invoke(funcName, v, invokeArgs);
        if (res) pushData(res);
        else pushData(agentc::createNullValue());
    }

    writeFrameIp(peek(VMRES_CODE), static_cast<int>(instruction_ptr));
    return true;
}

// Phase 4: Source-code EVAL.
// The value is a string (or bracketed thunk) — compile it and push a code frame.
void EdictVM::evalDispatchSource(CPtr<agentc::ListreeValue> v) {
    if (!v->getData()) {
        tail_eval = false;
        writeFrameIp(peek(VMRES_CODE), static_cast<int>(instruction_ptr));
        return;
    }

    std::string src(static_cast<char*>(v->getData()), v->getLength());

    if (edictTraceEnabled()) std::cout << "EVAL src: " << src << std::endl;

    // Strip outer brackets if they enclose the entire string.
    if (src.length() >= 2 && src.front() == '[' && src.back() == ']') {
        int depth = 0;
        bool fullEnclosure = true;
        for (size_t i = 0; i < src.length() - 1; ++i) {
            if (src[i] == '[') depth++;
            else if (src[i] == ']') depth--;
            if (depth == 0) { fullEnclosure = false; break; }
        }
        if (fullEnclosure) src = src.substr(1, src.length() - 2);
    }

    if (edictTraceEnabled()) std::cout << "EVAL compiling: " << src << std::endl;

    EdictCompiler cmp;
    try {
        BytecodeBuffer bc = cmp.compile(src);
        if (edictTraceEnabled()) std::cout << "EVAL compiled bytes: " << bc.getData().size() << std::endl;
        if (tail_eval) {
            popCodeFrame();
            tail_eval = false;
        }
        pushCodeFrame(bc);
    } catch (...) { setError("Eval error"); }
}

// ---------------------------------------------------------------------------
// op_EVAL — clean dispatch across the four evaluation phases.
// ---------------------------------------------------------------------------
void EdictVM::op_EVAL() {
    auto v = popData();
    if (!v) {
        tail_eval = false;
        writeFrameIp(peek(VMRES_CODE), static_cast<int>(instruction_ptr));
        return;
    }

    if (edictTraceEnabled())
        std::cerr << "op_EVAL: " << formatValueForDisplay(v)
                  << " list=" << v->isListMode() << " len=" << v->getLength() << std::endl;

    if (evalDispatchIterator(v)) return;
    if (evalDispatchThunk(v))    return;
    if (evalDispatchFFI(v))      return;
    evalDispatchSource(v);
}

void EdictVM::op_CTX_PUSH() { 
    auto ctx = popData();
    enq(VMRES_STACK, agentc::createListValue());
    enq(VMRES_DICT, agentc::createListValue());
    stack_enq(VMRES_DICT, ctx ? ctx : agentc::createNullValue()); 
}

void EdictVM::op_CTX_POP() { 
    listcat(VMRES_STACK);
    // ctx_pop should restore the previous context node to the stack?
    // In j2, it pops the context node and pushes it to data stack.
    auto frame = deq(VMRES_DICT, true); 
    if (frame) {
        auto ctx = frame->get(true, true);
        if (ctx) pushData(ctx);
    }
}

void EdictVM::op_FUN_PUSH() { 
    auto f = popData();
    if (f) enq(VMRES_FUNC, f);
    enq(VMRES_STACK, agentc::createListValue()); // New data stack frame
    enq(VMRES_DICT, agentc::createListValue());  // New dictionary stack frame
    // Pushing a fresh NullValue ensures this frame has its own private scope
    stack_enq(VMRES_DICT, agentc::createNullValue()); 
}

void EdictVM::op_FUN_POP() { 
    // Merge remaining results to parent stack
    listcat(VMRES_STACK);
    // 2. Release whole local scope frame
    deq(VMRES_DICT, true); 
    // 3. Release function from FUN stack
    deq(VMRES_FUNC, true); 
}

void EdictVM::op_FUN_EVAL() {
    // 1. Push FUN_POP to the code stack to clean up after the function returns
    BytecodeBuffer bc;
    bc.addOp(VMOP_FUN_POP);
    pushCodeFrame(bc);

    // 2. Evaluate function from FUN stack in its own VMOP_EVAL frame.
    // Using a dedicated frame keeps instruction pointers coherent with tail-eval logic.
    auto f = peek(VMRES_FUNC);
    if (f) {
        pushData(f);
        BytecodeBuffer eval_bc;
        eval_bc.addOp(VMOP_EVAL);
        pushCodeFrame(eval_bc);
    }
}

void EdictVM::op_FRAME_PUSH() { enq(VMRES_STACK, agentc::createListValue()); }
void EdictVM::op_FRAME_MERGE() { listcat(VMRES_STACK); }

static bool isTrue(CPtr<agentc::ListreeValue> v) {
    if (!v) return false;
    if ((v->getFlags() & agentc::LtvFlags::Null) != agentc::LtvFlags::None) return false;
    if (v->isListMode()) {
        bool empty = true;
        v->forEachList([&](CPtr<agentc::ListreeValueRef>&){ empty = false; });
        return !empty;
    }
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        if (v->getLength() == sizeof(int)) {
            int val; std::memcpy(&val, v->getData(), sizeof(int));
            return val != 0;
        }
        return v->getLength() > 0;
    }
    return v->getData() && v->getLength() > 0;
}

void EdictVM::op_FAIL() {
    auto v = popData();
    if (v) {
        // std::cerr << "FAIL: Pushing to State Stack: " << formatValueForDisplay(v) << std::endl;
        enq(VMRES_STATE, v);
    }
}

void EdictVM::op_TEST() {
    auto v = popData();
    if (!isTrue(v)) {
        // std::cerr << "TEST: Failed (" << formatValueForDisplay(v) << "). Pushing to State Stack." << std::endl;
        if (!v) v = agentc::createNullValue();
        enq(VMRES_STATE, v);
    }
}

void EdictVM::op_THROW() { // op_AND (&)
    // Check State Stack
    auto err = deq(VMRES_STATE, true); // Pop state
    // std::cerr << "AND (&) Check State Stack. Found: " << (err ? formatValueForDisplay(err) : "Empty") << std::endl;
    if (err) {
        // Failure: enable scan mode to skip the then-branch until CATCH.
        state |= VM_SCANNING;
        scan_mode = ScanMode::FromThrow;
        scan_depth = 0;
    }
    // Else: Success. Continue.
}

void EdictVM::op_CATCH() { // op_OR (|)
    // If we reached here (Normal), skip Else.
    state |= VM_SCANNING;
    scan_mode = ScanMode::FromCatch;
    scan_depth = 0;
}

void EdictVM::op_S2S() { pushData(peekData()); }
void EdictVM::op_D2S() { pushData(stack_deq(VMRES_DICT, false)); }
void EdictVM::op_E2S() { pushData(peek(VMRES_EXCP)); }
void EdictVM::op_F2S() { pushData(peek(VMRES_FUNC)); }
void EdictVM::op_S2D() { stack_enq(VMRES_DICT, popData()); }
void EdictVM::op_S2E() { enq(VMRES_EXCP, popData()); }
void EdictVM::op_S2F() { enq(VMRES_FUNC, popData()); }

void EdictVM::op_PRINT() {
    auto v = popData();
    if (v) std::cout << formatValueForDisplay(v) << std::endl;
}

void EdictVM::op_FREEZE() {
    // Pop top of data stack, freeze it (read-only, recursive), push it back.
    // After freeze the node and all non-Binary descendants are permanently immutable
    // and safe to share across VMs/threads without synchronisation.
    // Binary nodes (bytecode/thunk frames) are intentionally skipped by setReadOnly.
    auto v = popData();
    if (v && !v->isReadOnly()) v->setReadOnly(true);
    pushData(v);
}

void EdictVM::op_TO_JSON() {
    auto v = popData();
    pushData(agentc::createStringValue(agentc::toJson(v)));
}

void EdictVM::op_FROM_JSON() {
    auto v = popData();
    std::string json;
    if (!valueToString(v, json)) {
        setError("from_json expects a JSON string");
        return;
    }
    auto result = agentc::fromJson(json);
    if (!result) {
        setError("from_json: invalid JSON");
        return;
    }
    pushData(result);
}

void EdictVM::op_HEAP_UTILIZATION() {
    using agentc::ListreeValue;
    using agentc::ListreeItem;
    using agentc::ListreeValueRef;

    // Generic lambda — accepts any AllocatorStats (all have identical fields).
    auto printAllocStats = [](const char* label, const auto& s) {
        if (s.activeSlabCount == 0) {
            std::cout << "  " << label << ": empty" << std::endl;
            return;
        }
        size_t totalBytes = static_cast<size_t>(s.totalCapacitySlots) * s.itemSizeBytes;
        size_t usedBytes  = static_cast<size_t>(s.liveSlotCount)      * s.itemSizeBytes;
        double pct = s.totalCapacitySlots > 0
            ? 100.0 * s.liveSlotCount / s.totalCapacitySlots : 0.0;
        std::cout << "  " << label << ": "
                  << s.activeSlabCount << " slab(s), "
                  << s.liveSlotCount << "/" << s.totalCapacitySlots
                  << " slots live (" << static_cast<int>(pct + 0.5) << "%), "
                  << usedBytes / 1024 << " KB used / "
                  << totalBytes / 1024 << " KB reserved";
        if (s.slabs.size() > 1) {
            std::cout << std::endl << "    per-slab:";
            for (const auto& sl : s.slabs) {
                double sp = 100.0 * sl.liveSlots / SLAB_SIZE;
                std::cout << " [slab " << sl.slabIndex << ": "
                          << sl.liveSlots << "/" << SLAB_SIZE
                          << " (" << static_cast<int>(sp + 0.5) << "%)]";
            }
        }
        std::cout << std::endl;
    };

    std::cout << "=== HeapUtilization ===" << std::endl;
    printAllocStats("ListreeValue      ", Allocator<ListreeValue>::getAllocator().getStats());
    printAllocStats("ListreeItem       ", Allocator<ListreeItem>::getAllocator().getStats());
    printAllocStats("ListreeValueRef   ", Allocator<ListreeValueRef>::getAllocator().getStats());

    {
        const auto& blob = BlobAllocator::getAllocator().getBlobStats();
        if (blob.activeSlabs == 0) {
            std::cout << "  BlobAllocator     : empty" << std::endl;
        } else {
            double pct = blob.totalBytes > 0
                ? 100.0 * blob.usedBytes / blob.totalBytes : 0.0;
            std::cout << "  BlobAllocator     : "
                      << blob.activeSlabs << " slab(s), "
                      << blob.usedBytes / 1024 << " KB used / "
                      << blob.totalBytes / 1024 << " KB reserved"
                      << " (" << static_cast<int>(pct + 0.5) << "%)" << std::endl;
        }
    }
}

void EdictVM::op_CONCAT() {
    auto b = popData();
    auto a = popData();
    if (!a || !b) { setError("Underflow"); return; }
    std::string as, bs;
    if (!valueToString(a, as)) as = a && a->getData() ? std::string((char*)a->getData(), a->getLength()) : "";
    if (!valueToString(b, bs)) bs = b && b->getData() ? std::string((char*)b->getData(), b->getLength()) : "";
    pushData(agentc::createStringValue(as + bs));
}

void EdictVM::op_LIST_ADD() {
    auto val = popData();
    auto node = popData();
    if (!node || !val) { setError("Underflow"); return; }
    agentc::addListItem(node, val);
    pushData(node);
}

int EdictVM::runCodeLoop(size_t stopCodeDepth, bool markCompleteOnDrain) {
#if defined(__GNUC__) || defined(__clang__)
    static void* dispatch[] = {
        &&op_RESET, // Index 0
        &&op_YIELD, &&op_PUSHEXT, &&op_SPLICE,
        &&op_DUP, &&op_SWAP, &&op_POP, &&op_REF,
        &&op_ASSIGN, &&op_REMOVE, &&op_EVAL,
        &&op_CTX_PUSH, &&op_CTX_POP, &&op_FUN_PUSH, &&op_FUN_EVAL,
        &&op_FUN_POP, &&op_FRAME_PUSH, &&op_FRAME_MERGE, &&op_THROW,
        &&op_CATCH, &&op_S2S, &&op_D2S, &&op_E2S,
        &&op_F2S, &&op_S2D, &&op_S2E, &&op_S2F,
        &&op_CONCAT, &&op_LIST_ADD, &&op_FAIL, &&op_TEST,
        &&op_MAP, &&op_LOAD, &&op_IMPORT, &&op_IMPORT_RESOLVED,
        &&op_IMPORT_DEFERRED, &&op_IMPORT_COLLECT, &&op_IMPORT_STATUS,
        &&op_PARSE_JSON, &&op_MATERIALIZE_JSON, &&op_RESOLVE_JSON, &&op_IMPORT_RESOLVED_JSON,
        &&op_READ_TEXT, &&op_REQUEST_ID,
        &&op_BOOTSTRAP_CURATE_PARSER, &&op_BOOTSTRAP_CURATE_RESOLVER,
        &&op_BOOTSTRAP_CURATE_CARTOGRAPHER,
        &&op_CLOSURE, &&op_REWRITE_DEFINE,
        &&op_REWRITE_LIST, &&op_REWRITE_REMOVE, &&op_REWRITE_APPLY,
        &&op_REWRITE_MODE, &&op_REWRITE_TRACE, &&op_SPECULATE,
        &&op_UNSAFE_EXTENSIONS_ALLOW, &&op_UNSAFE_EXTENSIONS_BLOCK,
        &&op_UNSAFE_EXTENSIONS_STATUS, &&op_PRINT, &&op_HEAP_UTILIZATION,
        &&op_CURSOR_DOWN, &&op_CURSOR_UP, &&op_CURSOR_NEXT,
        &&op_CURSOR_PREV, &&op_CURSOR_GET, &&op_CURSOR_SET,
        &&op_FREEZE,
        &&op_TO_JSON,
        &&op_FROM_JSON,
    };
    // Verify dispatch table has exactly one entry per opcode. If this fires,
    // an opcode was added to VMOpcode without a corresponding dispatch entry.
    static_assert(sizeof(dispatch)/sizeof(dispatch[0]) == VMOP_COUNT,
                  "dispatch table size does not match VMOP_COUNT — add a handler for the new opcode");
#endif
    while (!(state & (VM_ERROR | VM_YIELD))) {
        if (getResourceDepth(VMRES_CODE) <= stopCodeDepth) {
            break;
        }
        auto cur = peek(VMRES_CODE);
        if (!cur) break;
        if (!cur->getData() || cur->getLength() == 0) {
            deq(VMRES_CODE, true);
            if (state & VM_SCANNING) {
                state &= ~VM_SCANNING;
                scan_mode = ScanMode::None;
                scan_depth = 0;
            }
            continue;
        }
        int ip = readFrameIp(cur);
        if (ip < 0) ip = 0;
        
        // If we reached the end of this frame, pop it and continue with the next
        if (static_cast<size_t>(ip) >= cur->getLength()) { 
            deq(VMRES_CODE, true);
            if (state & VM_SCANNING) {
                state &= ~VM_SCANNING;
                scan_mode = ScanMode::None;
                scan_depth = 0;
            }
            continue; 
        }

        code_ptr = static_cast<const uint8_t*>(cur->getData());
        code_size = cur->getLength();
        instruction_ptr = static_cast<size_t>(ip);

        // Peek at next byte without advancing yet to check for EOF safely
        if (instruction_ptr >= code_size) {
            deq(VMRES_CODE, true);
            continue;
        }

        uint8_t op = readByte();
        bool allow_rewrite_epilogue = op != VMOP_REWRITE_APPLY
                                   && op != VMOP_REWRITE_MODE
                                   && op != VMOP_REWRITE_TRACE
                                   && op != VMOP_SPECULATE;
        
        // DEBUG LOGGING
        if (edictTraceEnabled()) {
            std::cout << "OP: " << (int)op << " IP: " << (instruction_ptr-1) << " Stack: " << getStackSize() << std::endl;
        }
        
        if (state & VM_ERROR) break;

        // Scanning Logic
        if (state & VM_SCANNING) {
            bool keep_scanning = true;
            bool dispatch_after_scan = false;

            if (op == VMOP_PUSHEXT) {
                readValue(); // Skip payload
            } else if (op == VMOP_THROW) {
                ++scan_depth;
            } else if (op == VMOP_CATCH) {
                if (scan_depth == 0) {
                    state &= ~VM_SCANNING;
                    if (scan_mode == ScanMode::FromCatch) {
                        dispatch_after_scan = true;
                    } else {
                        scan_mode = ScanMode::None;
                        scan_depth = 0;
                    }
                    keep_scanning = false;
                } else {
                    --scan_depth;
                }
            }

            if (keep_scanning) {
                if (!tail_eval && (op != VMOP_EVAL || instruction_ptr < code_size)) {
                    writeFrameIp(cur, static_cast<int>(instruction_ptr));
                }
                continue;
            }

            if (!dispatch_after_scan) {
                if (!tail_eval && (op != VMOP_EVAL || instruction_ptr < code_size)) {
                    writeFrameIp(cur, static_cast<int>(instruction_ptr));
                }
                continue;
            }

            // Re-dispatch this opcode now that scan mode has completed.
            scan_mode = ScanMode::None;
            scan_depth = 0;
        }

        const bool skip_ip_write = (op == VMOP_EVAL && instruction_ptr >= code_size);
        tail_eval = skip_ip_write;

        if (op < VMOP_COUNT) goto *dispatch[op];
        goto op_INVALID;
op_RESET: op_RESET(); goto op_epilogue;
op_YIELD: op_YIELD(); goto op_epilogue;
op_PUSHEXT: op_PUSHEXT(); goto op_epilogue;
op_SPLICE: op_SPLICE(); goto op_epilogue;
op_DUP: op_DUP(); goto op_epilogue;
op_SWAP: op_SWAP(); goto op_epilogue;
op_POP: op_POP(); goto op_epilogue;
op_EVAL: op_EVAL(); goto op_epilogue;
op_REF: op_REF(); goto op_epilogue;
op_ASSIGN: op_ASSIGN(); goto op_epilogue;
op_REMOVE: op_REMOVE(); goto op_epilogue;
op_CTX_PUSH: op_CTX_PUSH(); goto op_epilogue;
op_CTX_POP: op_CTX_POP(); goto op_epilogue;
op_FUN_PUSH: op_FUN_PUSH(); goto op_epilogue;
op_FUN_EVAL: op_FUN_EVAL(); goto op_epilogue;
op_FUN_POP: op_FUN_POP(); goto op_epilogue;
op_FRAME_PUSH: op_FRAME_PUSH(); goto op_epilogue;
op_FRAME_MERGE: op_FRAME_MERGE(); goto op_epilogue;
op_THROW: op_THROW(); goto op_epilogue;
op_CATCH: op_CATCH(); goto op_epilogue;
op_S2S: op_S2S(); goto op_epilogue;
op_D2S: op_D2S(); goto op_epilogue;
op_E2S: op_E2S(); goto op_epilogue;
op_F2S: op_F2S(); goto op_epilogue;
op_S2D: op_S2D(); goto op_epilogue;
op_S2E: op_S2E(); goto op_epilogue;
op_S2F: op_S2F(); goto op_epilogue;
op_CONCAT: op_CONCAT(); goto op_epilogue;
op_LIST_ADD: op_LIST_ADD(); goto op_epilogue;
op_FAIL: op_FAIL(); goto op_epilogue;
op_TEST: op_TEST(); goto op_epilogue;
op_MAP: op_MAP(); goto op_epilogue;
op_LOAD: op_LOAD(); goto op_epilogue;
op_IMPORT: op_IMPORT(); goto op_epilogue;
op_IMPORT_RESOLVED: op_IMPORT_RESOLVED(); goto op_epilogue;
op_IMPORT_DEFERRED: op_IMPORT_DEFERRED(); goto op_epilogue;
op_IMPORT_COLLECT: op_IMPORT_COLLECT(); goto op_epilogue;
op_IMPORT_STATUS: op_IMPORT_STATUS(); goto op_epilogue;
op_PARSE_JSON: op_PARSE_JSON(); goto op_epilogue;
op_MATERIALIZE_JSON: op_MATERIALIZE_JSON(); goto op_epilogue;
op_RESOLVE_JSON: op_RESOLVE_JSON(); goto op_epilogue;
op_IMPORT_RESOLVED_JSON: op_IMPORT_RESOLVED_JSON(); goto op_epilogue;
op_READ_TEXT: op_READ_TEXT(); goto op_epilogue;
op_REQUEST_ID: op_REQUEST_ID(); goto op_epilogue;
op_BOOTSTRAP_CURATE_PARSER: op_BOOTSTRAP_CURATE_PARSER(); goto op_epilogue;
op_BOOTSTRAP_CURATE_RESOLVER: op_BOOTSTRAP_CURATE_RESOLVER(); goto op_epilogue;
 op_BOOTSTRAP_CURATE_CARTOGRAPHER: op_BOOTSTRAP_CURATE_CARTOGRAPHER(); goto op_epilogue;
op_CLOSURE: op_CLOSURE(); goto op_epilogue;
op_REWRITE_DEFINE: op_REWRITE_DEFINE(); goto op_epilogue;
op_REWRITE_LIST: op_REWRITE_LIST(); goto op_epilogue;
op_REWRITE_REMOVE: op_REWRITE_REMOVE(); goto op_epilogue;
op_REWRITE_APPLY: op_REWRITE_APPLY(); goto op_epilogue;
op_REWRITE_MODE: op_REWRITE_MODE(); goto op_epilogue;
op_REWRITE_TRACE: op_REWRITE_TRACE(); goto op_epilogue;
op_SPECULATE: op_SPECULATE(); goto op_epilogue;
op_UNSAFE_EXTENSIONS_ALLOW: op_UNSAFE_EXTENSIONS_ALLOW(); goto op_epilogue;
op_UNSAFE_EXTENSIONS_BLOCK: op_UNSAFE_EXTENSIONS_BLOCK(); goto op_epilogue;
op_UNSAFE_EXTENSIONS_STATUS: op_UNSAFE_EXTENSIONS_STATUS(); goto op_epilogue;
op_PRINT: op_PRINT(); goto op_epilogue;
op_HEAP_UTILIZATION: op_HEAP_UTILIZATION(); goto op_epilogue;
op_CURSOR_DOWN: op_CURSOR_DOWN(); goto op_epilogue;
op_CURSOR_UP: op_CURSOR_UP(); goto op_epilogue;
op_CURSOR_NEXT: op_CURSOR_NEXT(); goto op_epilogue;
op_CURSOR_PREV: op_CURSOR_PREV(); goto op_epilogue;
op_CURSOR_GET: op_CURSOR_GET(); goto op_epilogue;
op_CURSOR_SET: op_CURSOR_SET(); goto op_epilogue;
op_FREEZE: op_FREEZE(); goto op_epilogue;
op_TO_JSON: op_TO_JSON(); goto op_epilogue;
op_FROM_JSON: op_FROM_JSON(); goto op_epilogue;
op_INVALID: setError("Op " + std::to_string(op)); goto op_epilogue;
op_epilogue:
        if (allow_rewrite_epilogue && !(state & (VM_ERROR | VM_YIELD | VM_SCANNING))) applyRewriteLoop();
        if (!skip_ip_write) writeFrameIp(cur, static_cast<int>(instruction_ptr));
        continue;
    }
    code_ptr = nullptr;
    code_size = 0;
    if (markCompleteOnDrain && !(state & (VM_ERROR | VM_YIELD)) && getResourceDepth(VMRES_CODE) <= stopCodeDepth) {
        state |= VM_COMPLETE;
    }
    return state;
}

int EdictVM::execute(const BytecodeBuffer& code) {
    state &= ~(VM_ERROR | VM_YIELD | VM_COMPLETE | VM_SCANNING);
    error_message.clear();
    instruction_ptr = 0;
    tail_eval = false;
    scan_mode = ScanMode::None;
    scan_depth = 0;
    resources[VMRES_CODE] = agentc::createListValue();
    CPtr<agentc::ListreeValue> frame = makeCodeFrame(code);
    if (!frame) { setError("Code frame alloc"); return state; }
    enq(VMRES_CODE, frame);
    return runCodeLoop(0, true);
}

int EdictVM::executeNested(const BytecodeBuffer& code) {
    state &= ~(VM_ERROR | VM_YIELD | VM_COMPLETE | VM_SCANNING);
    error_message.clear();
    scan_mode = ScanMode::None;
    scan_depth = 0;

    const size_t parentDepth = getResourceDepth(VMRES_CODE);
    CPtr<agentc::ListreeValue> frame = makeCodeFrame(code);
    if (!frame) {
        setError("Code frame alloc");
        return state;
    }
    enq(VMRES_CODE, frame);
    return runCodeLoop(parentDepth, false);
}
    
    void EdictVM::registerCursorOperations() {
        auto dictVal = stack_deq(VMRES_DICT, false);
        if (!dictVal) return;

        // Create a "cursor" capsule in the global dictionary so Edict code
        // can navigate the VM's Listree cursor via e.g. "cursor.down !".
        auto capsule = agentc::createNullValue();
        addBuiltinThunk(capsule, "down", VMOP_CURSOR_DOWN);
        addBuiltinThunk(capsule, "up",   VMOP_CURSOR_UP);
        addBuiltinThunk(capsule, "next", VMOP_CURSOR_NEXT);
        addBuiltinThunk(capsule, "prev", VMOP_CURSOR_PREV);
        addBuiltinThunk(capsule, "get",  VMOP_CURSOR_GET);
        addBuiltinThunk(capsule, "set",  VMOP_CURSOR_SET);
        agentc::addNamedItem(dictVal, "cursor", capsule);
    }

    // ---- Cursor op implementations ----------------------------------------

    // Push a boolean Listree value (string "true"/"false") onto the data stack.
    // Using a string representation keeps the cursor result composable with
    // existing Edict string/test infrastructure.
    static CPtr<agentc::ListreeValue> makeBoolValue(bool b) {
        return agentc::createStringValue(b ? "true" : "false");
    }

    void EdictVM::op_CURSOR_DOWN() {
        bool ok = cursor.down();
        pushData(makeBoolValue(ok));
    }

    void EdictVM::op_CURSOR_UP() {
        bool ok = cursor.up();
        pushData(makeBoolValue(ok));
    }

    void EdictVM::op_CURSOR_NEXT() {
        bool ok = cursor.next();
        pushData(makeBoolValue(ok));
    }

    void EdictVM::op_CURSOR_PREV() {
        bool ok = cursor.prev();
        pushData(makeBoolValue(ok));
    }

    void EdictVM::op_CURSOR_GET() {
        // Push the current cursor node value onto the data stack.
        // If the cursor is not positioned on a node, push null.
        auto val = cursor.getValue();
        pushData(val ? val : agentc::createNullValue());
    }

    void EdictVM::op_CURSOR_SET() {
        // Pop the top of the data stack and assign it to the current cursor
        // node position using the same Cursor::assign() path as op_ASSIGN.
        auto val = popData();
        if (!val) { setError("CURSOR_SET: stack underflow"); return; }
        if (!cursor.assign(val)) {
            setError("CURSOR_SET: cursor not positioned on an assignable node");
        }
    }

    struct closure_context {
        explicit closure_context(CPtr<agentc::ListreeValue> continuation) : continuation(continuation) {}
        CPtr<agentc::ListreeValue> continuation;
    };

    static void destroy_closure_context(void* user_data) {
        auto* continuation = static_cast<closure_context*>(user_data);
        delete continuation;
    }

    static CPtr<agentc::ListreeValue> get_named_value(CPtr<agentc::ListreeValue> node, const std::string& name) {
        if (!node || node->isListMode()) return nullptr;
        auto item = node->find(name);
        if (!item) return nullptr;
        return item->getValue(false, false);
    }

    static void bind_named_value(CPtr<agentc::ListreeValue> node, const std::string& name, CPtr<agentc::ListreeValue> value) {
        if (!node || node->isListMode()) return;
        auto item = node->find(name, true);
        if (!item) return;
        item->addValue(value, false);
    }

    static void preload_imported_libraries(EdictVM& vm, CPtr<agentc::ListreeValue> scope) {
        if (!scope || scope->isListMode()) return;
        scope->forEachTree([&](const std::string& key, CPtr<agentc::ListreeItem>& item) {
            auto value = item ? item->getValue(false, false) : nullptr;
            if (!value || value->isListMode()) return;
            auto meta = get_named_value(value, "__cartographer");
            auto lib = get_named_value(meta, "library");
            if (!lib || !lib->getData()) return;
            std::string path(static_cast<const char*>(lib->getData()), lib->getLength());
            (void)key;
            vm.ffi->loadLibrary(path);
        });
    }
    
    static void closure_thunk(ffi_cif* cif, void* ret, void** args, void* user_data) {
        auto* continuation_ref = static_cast<closure_context*>(user_data);
        if (!continuation_ref || !continuation_ref->continuation) return;

        CPtr<agentc::ListreeValue> continuation = continuation_ref->continuation;
        CPtr<agentc::ListreeValue> agentFunction = get_named_value(continuation, "THUNK");
        CPtr<agentc::ListreeValue> root = get_named_value(continuation, "ROOT");
        if (!agentFunction) return;
        if (root) root = root->copy();
        else root = agentc::createNullValue();

        EdictVM vm(root);
        preload_imported_libraries(vm, root);

        // Mirror J2 callback bindings by exposing ARGi and RETURN in callback scope.
        for (unsigned i = 0; i < cif->nargs; ++i) {
            auto ltv = vm.ffi->convertReturn(args[i], cif->arg_types[i]);
            vm.pushData(ltv);
            bind_named_value(root, "ARG" + std::to_string(i), ltv ? ltv : agentc::createNullValue());
        }
        bind_named_value(root, "RETURN", agentc::createNullValue());

        vm.pushData(agentFunction);

        BytecodeBuffer bc;
        bc.addOp(VMOP_EVAL);
        vm.execute(bc);

        auto res = vm.popData();
        bind_named_value(root, "RETURN", res ? res : agentc::createNullValue());
        if (res) {
            if (agentc::cartographer::FFI::isLtvType(cif->rtype)) {
                auto handle = agentc::cptr_to_ltv(res);
                ltv_ref(handle);
            }
            vm.ffi->convertValue(res, cif->rtype, ret);
        } else if (ret) {
            if (cif->rtype == &ffi_type_sint) *(int*)ret = 0;
            else if (cif->rtype == &ffi_type_double) *(double*)ret = 0.0;
            else if (cif->rtype == &ffi_type_pointer) *(void**)ret = nullptr;
        }
    }

    CPtr<agentc::ListreeValue> EdictVM::buildClosureValue(CPtr<agentc::ListreeValue> signature,
                                                      CPtr<agentc::ListreeValue> agentFunction) {
        if (!signature || !agentFunction) return nullptr;

        CPtr<agentc::ListreeValue> rootScope = stack_deq(VMRES_DICT, false);
        if (rootScope) rootScope = rootScope->copy();
        else rootScope = agentc::createNullValue();

        auto continuation = agentc::createNullValue();
        agentc::addNamedItem(continuation, "THUNK", agentFunction);
        agentc::addNamedItem(continuation, "ROOT", rootScope);
        agentc::addNamedItem(continuation, "SIGNATURE", signature);

        auto* continuation_ref = new closure_context(continuation);
        void* code = ffi->createClosure(signature, closure_thunk, continuation_ref, destroy_closure_context);
        if (!code) return nullptr;

        return agentc::createBinaryValue(&code, sizeof(void*));
    }
    
    void EdictVM::addBuiltinThunk(CPtr<agentc::ListreeValue> dictVal, const std::string& name, uint8_t opcode) {
        if (!dictVal) return;
        BytecodeBuffer bc;
        bc.addOp(static_cast<VMOpcode>(opcode));
        CPtr<agentc::ListreeValue> thunk = makeCodeFrame(bc);
        agentc::addNamedItem(dictVal, name, thunk);
    }

    void EdictVM::loadCoreBuiltins() {
        auto dictVal = stack_deq(VMRES_DICT, false);
        if (!dictVal) return;

        addBuiltinThunk(dictVal, "reset", VMOP_RESET);
        addBuiltinThunk(dictVal, "yield", VMOP_YIELD);
        addBuiltinThunk(dictVal, "dup", VMOP_DUP);
        addBuiltinThunk(dictVal, "swap", VMOP_SWAP);
        addBuiltinThunk(dictVal, "pop", VMOP_POP);
        addBuiltinThunk(dictVal, "ref", VMOP_REF);
        addBuiltinThunk(dictVal, "assign", VMOP_ASSIGN);
        addBuiltinThunk(dictVal, "remove", VMOP_REMOVE);
        addBuiltinThunk(dictVal, "!", VMOP_EVAL);
        addBuiltinThunk(dictVal, "print", VMOP_PRINT);
        addBuiltinThunk(dictVal, "fail", VMOP_FAIL);
        addBuiltinThunk(dictVal, "test", VMOP_TEST);
        addBuiltinThunk(dictVal, "ffi_closure", VMOP_CLOSURE);
        addBuiltinThunk(dictVal, "rewrite_define", VMOP_REWRITE_DEFINE);
        addBuiltinThunk(dictVal, "rewrite_list", VMOP_REWRITE_LIST);
        addBuiltinThunk(dictVal, "rewrite_remove", VMOP_REWRITE_REMOVE);
        addBuiltinThunk(dictVal, "rewrite_apply", VMOP_REWRITE_APPLY);
        addBuiltinThunk(dictVal, "rewrite_mode", VMOP_REWRITE_MODE);
        addBuiltinThunk(dictVal, "rewrite_trace", VMOP_REWRITE_TRACE);
        addBuiltinThunk(dictVal, "speculate", VMOP_SPECULATE);
        addBuiltinThunk(dictVal, "unsafe_extensions_allow", VMOP_UNSAFE_EXTENSIONS_ALLOW);
        addBuiltinThunk(dictVal, "unsafe_extensions_block", VMOP_UNSAFE_EXTENSIONS_BLOCK);
        addBuiltinThunk(dictVal, "unsafe_extensions_status", VMOP_UNSAFE_EXTENSIONS_STATUS);
        addBuiltinThunk(dictVal, "HeapUtilization", VMOP_HEAP_UTILIZATION);
        addBuiltinThunk(dictVal, "freeze", VMOP_FREEZE);
        addBuiltinThunk(dictVal, "to_json", VMOP_TO_JSON);
        addBuiltinThunk(dictVal, "from_json", VMOP_FROM_JSON);
    }

    void EdictVM::installBootstrapImportCapsule() {
        auto dictVal = stack_deq(VMRES_DICT, false);
        if (!dictVal) return;

        auto capsule = agentc::createNullValue();
        addBuiltinThunk(capsule, "curate_parser", VMOP_BOOTSTRAP_CURATE_PARSER);
        addBuiltinThunk(capsule, "curate_resolver", VMOP_BOOTSTRAP_CURATE_RESOLVER);
        addBuiltinThunk(capsule, "curate_cartographer", VMOP_BOOTSTRAP_CURATE_CARTOGRAPHER);
        addBuiltinThunk(capsule, "map", VMOP_MAP);
        addBuiltinThunk(capsule, "load", VMOP_LOAD);
        addBuiltinThunk(capsule, "import", VMOP_IMPORT);
        addBuiltinThunk(capsule, "import_resolved", VMOP_IMPORT_RESOLVED);
        addBuiltinThunk(capsule, "import_deferred", VMOP_IMPORT_DEFERRED);
        addBuiltinThunk(capsule, "import_collect", VMOP_IMPORT_COLLECT);
        addBuiltinThunk(capsule, "import_status", VMOP_IMPORT_STATUS);
        addBuiltinThunk(capsule, "parse_json", VMOP_PARSE_JSON);
        addBuiltinThunk(capsule, "materialize_json", VMOP_MATERIALIZE_JSON);
        addBuiltinThunk(capsule, "resolve_json", VMOP_RESOLVE_JSON);
        addBuiltinThunk(capsule, "import_resolved_json", VMOP_IMPORT_RESOLVED_JSON);
        addBuiltinThunk(capsule, "request_id", VMOP_REQUEST_ID);
        addBuiltinThunk(capsule, "freeze", VMOP_FREEZE);
        addBuiltinThunk(capsule, "to_json", VMOP_TO_JSON);
        addBuiltinThunk(capsule, "from_json", VMOP_FROM_JSON);
        agentc::addNamedItem(dictVal, "__bootstrap_import", capsule);
    }

    void EdictVM::loadBuiltins() {
        loadCoreBuiltins();
        installBootstrapImportCapsule();
        registerCursorOperations();
    }

    void EdictVM::runStartupBootstrapPrelude() {
        // The prelude source is a fixed string — compile it once and cache the
        // resulting bytecode.  Each VM still executes its own copy of the
        // bytecode (execution is inherently per-VM), but the parse+compile step
        // is paid only once across the process lifetime.
        static std::once_flag bootstrapCompiled;
        static BytecodeBuffer cachedBootstrap;
        std::call_once(bootstrapCompiled, []() {
            cachedBootstrap = EdictCompiler().compile(
                "__bootstrap_import.curate_parser ! @parser "
                "__bootstrap_import.curate_resolver ! @resolver "
                "__bootstrap_import.curate_cartographer ! @cartographer");
        });
        execute(cachedBootstrap);
    }

void EdictVM::op_CLOSURE() {
    auto agentFunction = popData();
    auto signature = popData();
    if (!agentFunction || !signature) { setError("Stack underflow for CLOSURE"); return; }

    auto closureValue = buildClosureValue(signature, agentFunction);
    if (closureValue) pushData(closureValue);
    else pushData(agentc::createNullValue());
}

} // namespace agentc::edict
