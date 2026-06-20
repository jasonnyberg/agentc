// edict_vm_treesitter.cpp — G094 Tree-Sitter VM operation implementations
//
// Implements VMOP_TS_LOAD, VMOP_TS_PARSE, and VMOP_TS_LIST for
// agent-level AST parsing from Edict.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "edict_vm.h"
#include "../treesitter/tree_sitter_bridge.h"
#include "../listree/listree.h"
#include <filesystem>
#include <cstdlib>

namespace agentc::edict {

// Local helper — mirrors the static valueToString in edict_vm_core.cpp
static bool tsValueToString(CPtr<agentc::ListreeValue> v, std::string& out) {
    if (!v || !v->getData() || v->getLength() == 0) return false;
    if ((v->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) return false;
    out.assign(static_cast<char*>(v->getData()), v->getLength());
    return true;
}

void EdictVM::op_TS_LOAD() {
    auto v = popData();
    std::string langName;
    if (!tsValueToString(v, langName)) {
        setError("TS_LOAD expects language name string");
        return;
    }
    std::string errorMsg;

    // Try standard library paths first
    if (tsBridge_->loadLanguageByName(langName, errorMsg)) {
        pushData(agentc::createStringValue("true"));
        return;
    }

    // Try as a direct library path
    std::filesystem::path p(langName);
    if (p.has_filename() && std::filesystem::exists(p)) {
        // Extract language name from path: libtree-sitter-<name>.so -> <name>
        std::string filename = p.filename().string();
        const std::string prefix = "libtree-sitter-";
        if (filename.substr(0, prefix.size()) == prefix) {
            std::string extracted = filename.substr(prefix.size());
            size_t dotPos = extracted.find('.');
            if (dotPos != std::string::npos) {
                extracted = extracted.substr(0, dotPos);
            }
            if (tsBridge_->loadLanguage(langName, extracted, errorMsg)) {
                pushData(agentc::createStringValue("true"));
                return;
            }
        }
    }

    setError("TS_LOAD failed: " + errorMsg);
}

void EdictVM::op_TS_PARSE() {
    auto sourceVal = popData();
    auto langVal = popData();

    std::string langName, source;
    if (!tsValueToString(langVal, langName)) {
        setError("TS_PARSE expects language name string");
        return;
    }
    if (!tsValueToString(sourceVal, source)) {
        setError("TS_PARSE expects source code string");
        return;
    }

    std::string errorMsg;
    auto ast = tsBridge_->parse(langName, source, errorMsg);
    if (!ast) {
        setError("TS_PARSE failed: " + errorMsg);
        return;
    }
    pushData(ast);
}

void EdictVM::op_TS_LIST() {
    auto langs = tsBridge_->listLanguages();
    auto list = agentc::createListValue();
    for (const auto& lang : langs) {
        agentc::addListItem(list, agentc::createStringValue(lang));
    }
    pushData(list);
}

} // namespace agentc::edict
