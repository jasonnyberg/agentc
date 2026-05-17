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
#include "../cartographer/ffi.h"
#include <cstdlib>
#include <iostream>
#include <mutex>

namespace agentc::edict {

namespace {

bool startupTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("AGENTC_EDICT_TRACE_STARTUP");
        return value && *value && std::string(value) != "0";
    }();
    return enabled;
}

void startupTrace(const std::string& marker) {
    if (startupTraceEnabled()) {
        std::cerr << "EDICT-STARTUP: " << marker << std::endl;
    }
}

void addBootstrapMetadata(CPtr<agentc::ListreeValue> object,
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

// Build a function-def LTV tree suitable for FFI dispatch, with all parameters
// and the return value typed as "ltv" (the SlabId passthrough sentinel).
// paramNames lists the parameter names in call order.
// If hasReturnValue is false, return_type is "void".
CPtr<agentc::ListreeValue> buildBoxingFuncDef(
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

} // namespace

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
                     "header parser.parse_json! @schema "
                     "schema parser.materialize_json!");
    addCompiledThunk(parser, "parse_json", "parser.__native.parse_json!");
    addCompiledThunk(parser, "materialize_json", "parser.__native.materialize_json!");
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

    addCompiledThunk(resolver, "load", "resolver.__native.load!");
    addCompiledThunk(resolver, "resolve_json", "resolver.__native.resolve_json!");
    addCompiledThunk(resolver, "import_resolved_json", "resolver.__native.import_resolved_json!");
    addCompiledThunk(resolver, "import", "resolver.__native.import!");
    addCompiledThunk(resolver, "import_resolved",
                     "@resolved_path "
                     "resolved_path resolver.__native.read_text! @resolved "
                     "resolved resolver.import_resolved_json!");
    addCompiledThunk(resolver, "import_deferred", "resolver.__native.import_deferred!");
    addCompiledThunk(resolver, "import_collect",
                     "@request "
                     "request resolver.__native.request_id! @request_id "
                     "request_id resolver.__native.import_collect!");
    addCompiledThunk(resolver, "import_status",
                     "@request "
                     "request resolver.__native.request_id! @request_id "
                     "request_id resolver.__native.import_status!");
    addBootstrapMetadata(resolver, "resolver");
    return resolver;
}

void EdictVM::op_BOOTSTRAP_CURATE_PARSER() {
    pushData(createBootstrapCuratedParser());
}

void EdictVM::op_BOOTSTRAP_CURATE_RESOLVER() {
    pushData(createBootstrapCuratedResolver());
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
    //   cartographer.box!      — ( source_ltv type_def -- boxed )
    //   cartographer.unbox!    — ( boxed -- unboxed_ltv )
    //   cartographer.box_free! — ( boxed -- )
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
    addBuiltinThunk(dictVal, "ref", VMOP_REF);
    addBuiltinThunk(dictVal, "assign", VMOP_ASSIGN);
    addBuiltinThunk(dictVal, "remove", VMOP_REMOVE);
    addBuiltinThunk(dictVal, "remove_head", VMOP_REMOVE_HEAD);
    addBuiltinThunk(dictVal, "!", VMOP_EVAL);
    addBuiltinThunk(dictVal, ".", VMOP_PRINT);
    addBuiltinThunk(dictVal, "print", VMOP_PRINT);
    addBuiltinThunk(dictVal, "fail", VMOP_FAIL);
    addBuiltinThunk(dictVal, "test", VMOP_TEST);
    addBuiltinThunk(dictVal, "lax", VMOP_LOOKUP_LAX);
    addBuiltinThunk(dictVal, "strict", VMOP_LOOKUP_STRICT_NULL);
    addBuiltinThunk(dictVal, "strict_null", VMOP_LOOKUP_STRICT_NULL);
    addBuiltinThunk(dictVal, "strict_fail", VMOP_LOOKUP_STRICT_FAIL);
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
    addBuiltinThunk(dictVal, "intern_run", VMOP_INTERN_RUN);
    addBuiltinThunk(dictVal, "intern_start", VMOP_INTERN_START);
    addBuiltinThunk(dictVal, "intern_sync", VMOP_INTERN_SYNC);
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
    addBuiltinThunk(capsule, "intern_run", VMOP_INTERN_RUN);
    addBuiltinThunk(capsule, "intern_start", VMOP_INTERN_START);
    addBuiltinThunk(capsule, "intern_sync", VMOP_INTERN_SYNC);
    agentc::addNamedItem(dictVal, "__bootstrap_import", capsule);
}

void EdictVM::loadBuiltins() {
    loadCoreBuiltins();
    installBootstrapImportCapsule();
    registerCursorOperations();
}

void EdictVM::runStartupBootstrapPrelude() {
    startupTrace("bootstrap-prelude-enter");
    // The prelude source is a fixed string — compile it once and cache the
    // resulting bytecode.  Each VM still executes its own copy of the
    // bytecode (execution is inherently per-VM), but the parse+compile step
    // is paid only once across the process lifetime.
    static std::once_flag bootstrapCompiled;
    static BytecodeBuffer cachedBootstrap;
    std::call_once(bootstrapCompiled, []() {
        cachedBootstrap = EdictCompiler().compile(
            "__bootstrap_import.curate_parser! @parser "
            "__bootstrap_import.curate_resolver! @resolver "
            "__bootstrap_import.curate_cartographer! @cartographer");
    });
    startupTrace("bootstrap-prelude-before-execute");
    execute(cachedBootstrap);
    startupTrace("bootstrap-prelude-after-execute");
}

} // namespace agentc::edict
