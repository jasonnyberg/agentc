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

#include "../edict_repl.h"
#include <gtest/gtest.h>
#include "../edict_vm.h"
#include "../edict_compiler.h"
#include "../../cartographer/ffi.h"
#include "../../cartographer/parser.h"
#include "../../cartographer/resolver.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <string>
#include <cstring>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace agentc::edict;

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif
#ifndef TEST_EDICT_BIN_DIR
#define TEST_EDICT_BIN_DIR "."
#endif

namespace {

struct ProcessResult {
    int exitCode = -1;
    std::string stdoutText;
    std::string stderrText;
};

static bool readAllFromFd(int fd, std::string& out) {
    out.clear();
    char buffer[4096];
    while (true) {
        ssize_t count = ::read(fd, buffer, sizeof(buffer));
        if (count == 0) {
            return true;
        }
        if (count < 0) {
            return false;
        }
        out.append(buffer, static_cast<size_t>(count));
    }
}

static CPtr<agentc::ListreeValue> makeArgList(std::initializer_list<std::string> values) {
    CPtr<agentc::ListreeValue> args = agentc::createListValue();
    for (const std::string& value : values) {
        args->put(agentc::createStringValue(value), false);
    }
    return args;
}

static size_t argCount(CPtr<agentc::ListreeValue> args) {
    size_t count = 0;
    if (!args) {
        return 0;
    }
    args->forEachList([&count](CPtr<agentc::ListreeValueRef>&) {
        ++count;
    });
    return count;
}

static std::string argAt(CPtr<agentc::ListreeValue> args, size_t index) {
    std::string result;
    if (!args) {
        return result;
    }

    size_t current = 0;
    args->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (current != index) {
            ++current;
            return;
        }

        CPtr<agentc::ListreeValue> value = ref->getValue();
        if (value && value->getData() && value->getLength() > 0) {
            result.assign(static_cast<const char*>(value->getData()), static_cast<size_t>(value->getLength()));
        }
        current = index + 1;
    });
    return result;
}

static ProcessResult runProcess(CPtr<agentc::ListreeValue> args) {
    ProcessResult result;
    const size_t argc = argCount(args);
    if (argc == 0) {
        result.stderrText = "No command provided";
        return result;
    }

    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    if (::pipe(stdoutPipe) != 0 || ::pipe(stderrPipe) != 0) {
        result.stderrText = "Failed to create process pipes";
        return result;
    }

    pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        ::dup2(stderrPipe[1], STDERR_FILENO);

        ::close(stdoutPipe[0]);
        ::close(stdoutPipe[1]);
        ::close(stderrPipe[0]);
        ::close(stderrPipe[1]);

        char** argv = new char*[argc + 1];
        for (size_t i = 0; i < argc; ++i) {
            std::string arg = argAt(args, i);
            argv[i] = new char[arg.size() + 1];
            std::memcpy(argv[i], arg.c_str(), arg.size() + 1);
        }
        argv[argc] = nullptr;
        ::execv(argv[0], argv);
        _exit(127);
    }

    ::close(stdoutPipe[1]);
    ::close(stderrPipe[1]);

    readAllFromFd(stdoutPipe[0], result.stdoutText);
    readAllFromFd(stderrPipe[0], result.stderrText);
    ::close(stdoutPipe[0]);
    ::close(stderrPipe[0]);

    int status = 0;
    if (::waitpid(pid, &status, 0) == pid) {
        if (WIFEXITED(status)) {
            result.exitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.exitCode = 128 + WTERMSIG(status);
        }
    }
    return result;
}

} // namespace

static CPtr<agentc::ListreeValue> make_callback_signature() {
    auto signature = agentc::createNullValue();
    agentc::addNamedItem(signature, "return_type", agentc::createStringValue("int"));

    auto children = agentc::createNullValue();
    for (int i = 0; i < 2; ++i) {
        auto parameter = agentc::createNullValue();
        agentc::addNamedItem(parameter, "kind", agentc::createStringValue("Parameter"));
        agentc::addNamedItem(parameter, "type", agentc::createStringValue("int"));
        agentc::addNamedItem(children, "p" + std::to_string(i), parameter);
    }
    agentc::addNamedItem(signature, "children", children);
    return signature;
}

static CPtr<agentc::ListreeValue> require_function_def(CPtr<agentc::ListreeValue> defs,
                                                       const std::string& functionName) {
    if (!defs) {
        ADD_FAILURE() << "Missing defs object";
        return nullptr;
    }
    auto functionItem = defs->find(functionName);
    if (!functionItem) {
        ADD_FAILURE() << "Missing function: " << functionName;
        return nullptr;
    }
    auto functionDef = functionItem->getValue(false, false);
    if (!functionDef) {
        ADD_FAILURE() << "Missing function def: " << functionName;
        return nullptr;
    }
    return functionDef;
}

static CPtr<agentc::ListreeValue> require_children(CPtr<agentc::ListreeValue> functionDef) {
    if (!functionDef) {
        ADD_FAILURE() << "Missing functionDef";
        return nullptr;
    }
    auto childrenItem = functionDef->find("children");
    if (!childrenItem) {
        ADD_FAILURE() << "Missing children item";
        return nullptr;
    }
    auto children = childrenItem->getValue(false, false);
    if (!children) {
        ADD_FAILURE() << "Missing children value";
        return nullptr;
    }
    return children;
}

static CPtr<agentc::ListreeValue> require_child(CPtr<agentc::ListreeValue> parent,
                                                const std::string& name) {
    if (!parent) {
        ADD_FAILURE() << "Missing parent for child: " << name;
        return nullptr;
    }
    auto item = parent->find(name);
    if (!item) {
        ADD_FAILURE() << "Missing child: " << name;
        return nullptr;
    }
    auto value = item->getValue(false, false);
    if (!value) {
        ADD_FAILURE() << "Missing child value: " << name;
        return nullptr;
    }
    return value;
}

static void set_type_field(CPtr<agentc::ListreeValue> node,
                           const std::string& field,
                           const std::string& type) {
    ASSERT_TRUE((bool)node);
    node->remove(field);
    agentc::addNamedItem(node, field, agentc::createStringValue(type));
}

static void replace_children_in_order(CPtr<agentc::ListreeValue> functionDef,
                                      const std::vector<std::pair<std::string, CPtr<agentc::ListreeValue>>>& orderedChildren) {
    ASSERT_TRUE((bool)functionDef);
    functionDef->remove("children");
    auto children = agentc::createNullValue();
    for (size_t i = 0; i < orderedChildren.size(); ++i) {
        auto child = orderedChildren[i].second;
        ASSERT_TRUE((bool)child);
        agentc::addNamedItem(children, "p" + std::to_string(i), child);
        agentc::addNamedItem(child, "name", agentc::createStringValue(orderedChildren[i].first));
    }
    agentc::addNamedItem(functionDef, "children", children);
}

static void normalize_thread_runtime_defs(CPtr<agentc::ListreeValue> defs) {
    auto spawn = require_function_def(defs, "agentc_thread_spawn_ltv");
    set_type_field(spawn, "return_type", "pointer");
    auto spawnChildren = require_children(spawn);
    auto entry = require_child(spawnChildren, "entry");
    auto arg = require_child(spawnChildren, "arg");
    set_type_field(entry, "type", "pointer");
    set_type_field(arg, "type", "ltv");
    replace_children_in_order(spawn, {{"entry", entry}, {"arg", arg}});

    auto join = require_function_def(defs, "agentc_thread_join_ltv");
    set_type_field(join, "return_type", "ltv");
    auto joinHandle = require_child(require_children(join), "handle");
    set_type_field(joinHandle, "type", "pointer");
    replace_children_in_order(join, {{"handle", joinHandle}});

    auto detach = require_function_def(defs, "agentc_thread_detach");
    set_type_field(detach, "return_type", "void");
    auto detachHandle = require_child(require_children(detach), "handle");
    set_type_field(detachHandle, "type", "pointer");
    replace_children_in_order(detach, {{"handle", detachHandle}});

    auto destroy = require_function_def(defs, "agentc_thread_destroy");
    set_type_field(destroy, "return_type", "void");
    auto destroyHandle = require_child(require_children(destroy), "handle");
    set_type_field(destroyHandle, "type", "pointer");
    replace_children_in_order(destroy, {{"handle", destroyHandle}});

    auto sharedCreate = require_function_def(defs, "agentc_shared_create_ltv");
    set_type_field(sharedCreate, "return_type", "pointer");
    auto sharedInitial = require_child(require_children(sharedCreate), "initial");
    set_type_field(sharedInitial, "type", "ltv");
    replace_children_in_order(sharedCreate, {{"initial", sharedInitial}});

    auto sharedDestroy = require_function_def(defs, "agentc_shared_destroy");
    set_type_field(sharedDestroy, "return_type", "void");
    auto sharedDestroyCell = require_child(require_children(sharedDestroy), "cell");
    set_type_field(sharedDestroyCell, "type", "pointer");
    replace_children_in_order(sharedDestroy, {{"cell", sharedDestroyCell}});

    auto sharedRead = require_function_def(defs, "agentc_shared_read_ltv");
    set_type_field(sharedRead, "return_type", "ltv");
    auto sharedReadCell = require_child(require_children(sharedRead), "cell");
    set_type_field(sharedReadCell, "type", "pointer");
    replace_children_in_order(sharedRead, {{"cell", sharedReadCell}});

    auto sharedWrite = require_function_def(defs, "agentc_shared_write_ltv");
    set_type_field(sharedWrite, "return_type", "int");
    auto sharedWriteChildren = require_children(sharedWrite);
    auto sharedWriteCell = require_child(sharedWriteChildren, "cell");
    auto sharedWriteReplacement = require_child(sharedWriteChildren, "replacement");
    set_type_field(sharedWriteCell, "type", "pointer");
    set_type_field(sharedWriteReplacement, "type", "ltv");
    replace_children_in_order(sharedWrite, {{"cell", sharedWriteCell}, {"replacement", sharedWriteReplacement}});

    // Freeze the defs tree so all threads can share it without copies.
    // This must be called AFTER all normalization is complete.
    if (defs && !defs->isReadOnly()) defs->setReadOnly(true);
}

static CPtr<agentc::ListreeValue> make_apply_op_definition() {
    auto definition = agentc::createNullValue();
    agentc::addNamedItem(definition, "kind", agentc::createStringValue("Function"));
    agentc::addNamedItem(definition, "name", agentc::createStringValue("apply_op"));
    agentc::addNamedItem(definition, "return_type", agentc::createStringValue("int"));

    auto children = agentc::createNullValue();

    auto p0 = agentc::createNullValue();
    agentc::addNamedItem(p0, "kind", agentc::createStringValue("Parameter"));
    agentc::addNamedItem(p0, "type", agentc::createStringValue("int"));
    agentc::addNamedItem(children, "p0", p0);

    auto p1 = agentc::createNullValue();
    agentc::addNamedItem(p1, "kind", agentc::createStringValue("Parameter"));
    agentc::addNamedItem(p1, "type", agentc::createStringValue("int"));
    agentc::addNamedItem(children, "p1", p1);

    auto p2 = agentc::createNullValue();
    agentc::addNamedItem(p2, "kind", agentc::createStringValue("Parameter"));
    agentc::addNamedItem(p2, "type", agentc::createStringValue("pointer"));
    agentc::addNamedItem(children, "p2", p2);

    agentc::addNamedItem(definition, "children", children);
    return definition;
}

static CPtr<agentc::ListreeValue> make_apply_op_definition_with_signature() {
    auto definition = make_apply_op_definition();
    auto children = definition->find("children");
    if (!children) return definition;

    auto childrenVal = children->getValue(false, false);
    if (!childrenVal) return definition;

    auto p2 = childrenVal->find("p2");
    if (p2 && p2->getValue()) {
        auto param = p2->getValue();
        agentc::addNamedItem(param, "signature", make_callback_signature());
    }
    return definition;
}

static CPtr<agentc::ListreeValue> make_imported_function_definition(const std::string& name,
                                                                const std::string& safety,
                                                                int parameterCount = 0,
                                                                const std::string& parameterType = "pointer") {
    auto definition = agentc::createNullValue();
    agentc::addNamedItem(definition, "kind", agentc::createStringValue("Function"));
    agentc::addNamedItem(definition, "name", agentc::createStringValue(name));
    agentc::addNamedItem(definition, "imported_via", agentc::createStringValue("cartographer_service"));
    agentc::addNamedItem(definition, "safety", agentc::createStringValue(safety));

    auto children = agentc::createNullValue();
    for (int i = 0; i < parameterCount; ++i) {
        auto parameter = agentc::createNullValue();
        agentc::addNamedItem(parameter, "kind", agentc::createStringValue("Parameter"));
        agentc::addNamedItem(parameter, "type", agentc::createStringValue(parameterType));
        agentc::addNamedItem(children, "p" + std::to_string(i), parameter);
    }
    agentc::addNamedItem(definition, "children", children);
    return definition;
}

static std::string decodeStringInt(const CPtr<agentc::ListreeValue>& value) {
    EXPECT_TRUE((bool)value);
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

static std::vector<std::string> listToStrings(const CPtr<agentc::ListreeValue>& value) {
    std::vector<std::string> out;
    if (!value || !value->isListMode()) {
        return out;
    }

    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue() && ref->getValue()->getData()) {
            out.emplace_back(static_cast<const char*>(ref->getValue()->getData()), ref->getValue()->getLength());
        }
    });
    std::reverse(out.begin(), out.end());
    return out;
}

static std::vector<CPtr<agentc::ListreeValue>> listItems(const CPtr<agentc::ListreeValue>& value) {
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

static void writeResolvedApi(const std::filesystem::path& libPath,
                             const std::filesystem::path& headerPath,
                             const std::filesystem::path& resolvedPath) {
    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(agentc::cartographer::parser::parseHeaderToDescription(
        mapper, headerPath.string(), description, error)) << error;

    agentc::cartographer::resolver::ResolvedApi resolved;
    ASSERT_TRUE(agentc::cartographer::resolver::resolveApiDescription(
        libPath.string(), description, resolved, error)) << error;

    std::ofstream output(resolvedPath);
    ASSERT_TRUE(output.good());
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    output.close();
}

static void expectCodeFrameLongerThanBuiltinThunk(CPtr<agentc::ListreeValue> value) {
    ASSERT_TRUE((bool)value);
    ASSERT_TRUE((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None);
    EXPECT_GT(value->getLength(), static_cast<size_t>(1));
}

TEST(CallbackTest, Closure) {
    EdictVM vm;

    std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";

    ASSERT_TRUE(vm.ffi->loadLibrary(libPath));
    auto applyDef = make_apply_op_definition();

    auto callbackSignature = make_callback_signature();
    auto callbackThunk = agentc::createStringValue("[ pop pop '42 ]");
    vm.pushData(callbackSignature);
    vm.pushData(callbackThunk);

    BytecodeBuffer buildClosure;
    buildClosure.addOp(VMOP_CLOSURE);
    int state = vm.execute(buildClosure);

    auto callbackPointer = vm.popData();
    ASSERT_TRUE((bool)callbackPointer);
    ASSERT_TRUE((callbackPointer->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None);
    ASSERT_EQ(callbackPointer->getLength(), sizeof(void*));

    int a = 10;
    int b = 20;
    auto args = agentc::createListValue();
    agentc::addListItem(args, agentc::createBinaryValue(&a, sizeof(int)));
    agentc::addListItem(args, agentc::createBinaryValue(&b, sizeof(int)));
    agentc::addListItem(args, callbackPointer);
    auto res = vm.ffi->invoke("apply_op", applyDef, args);

    ASSERT_TRUE((bool)res);
    std::string resStr(static_cast<const char*>(res->getData()), res->getLength());
    ASSERT_EQ(resStr, "42");
}

TEST(CallbackTest, ClosureUnderflow) {
    EdictVM vm;

    BytecodeBuffer buildClosure;
    buildClosure.addOp(VMOP_CLOSURE);
    int state = vm.execute(buildClosure);

    ASSERT_TRUE(state & VM_ERROR);
    ASSERT_NE(vm.getError().find("Stack underflow"), std::string::npos);
}

TEST(CallbackTest, ClosureFromParamSignature) {
    EdictVM vm;

    std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";

    ASSERT_TRUE(vm.ffi->loadLibrary(libPath));
    auto applyDef = make_apply_op_definition_with_signature();
    auto callbackThunk = agentc::createStringValue("[ pop pop '42 ]");

    int a = 10;
    int b = 20;
    vm.pushData(agentc::createBinaryValue(&a, sizeof(int)));
    vm.pushData(agentc::createBinaryValue(&b, sizeof(int)));
    vm.pushData(callbackThunk);
    vm.pushData(applyDef);

    BytecodeBuffer bc;
    bc.addOp(VMOP_EVAL);
    int state = vm.execute(bc);

    auto res = vm.popData();
    ASSERT_TRUE((bool)res);
    std::string resStr2(static_cast<const char*>(res->getData()), res->getLength());
    ASSERT_EQ(resStr2, "42");
}

TEST(CallbackTest, EdictBuiltinsMapLoadAndInvokeCartographerFunction) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";
    const std::string headerPath = std::string(TEST_SOURCE_DIR) + "/libagentmath_poc.h";
    const std::string source =
        "[" + libPath + "] resolver.load ! "
        "[" + headerPath + "] parser.map ! @defs "
        "10 32 defs.add !";

    BytecodeBuffer bc = compiler.compile(source);
    int state = vm.execute(bc);

    auto result = vm.popData();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(decodeStringInt(result), "42");
}

TEST(CallbackTest, LoadBuiltinReportsMissingLibrary) {
    EdictVM vm;
    EdictCompiler compiler;

    BytecodeBuffer bc = compiler.compile("'/tmp/definitely_missing_j3_lib.so resolver.load !");
    int state = vm.execute(bc);

    ASSERT_TRUE(state & VM_ERROR);
    ASSERT_NE(vm.getError().find("Failed to load library"), std::string::npos);
}

TEST(CallbackTest, BootstrapImportCapsuleOwnsImportSurface) {
    EdictVM vm;
    EdictCompiler compiler;

    BytecodeBuffer bc = compiler.compile("__bootstrap_import");
    int state = vm.execute(bc);

    auto capsule = vm.popData();
    ASSERT_TRUE((bool)capsule);
    ASSERT_FALSE(capsule->isListMode());

    for (const char* name : {"curate_parser", "curate_resolver", "map", "load", "import", "import_resolved", "import_deferred", "import_collect", "import_status", "parse_json", "materialize_json", "resolve_json", "import_resolved_json", "request_id"}) {
        auto item = capsule->find(name);
        ASSERT_TRUE(bool(item)) << name;
        auto value = item->getValue(false, false);
        ASSERT_TRUE((bool)value) << name;
    }

    bc = compiler.compile("parser resolver");
    state = vm.execute(bc);

    auto resolver = vm.popData();
    auto parser = vm.popData();
    ASSERT_TRUE((bool)parser);
    ASSERT_TRUE((bool)resolver);
    ASSERT_TRUE(bool(parser->find("map")));
    ASSERT_TRUE(bool(parser->find("parse_json")));
    ASSERT_TRUE(bool(parser->find("materialize_json")));
    ASSERT_TRUE(bool(parser->find("__native")));
    ASSERT_TRUE(bool(resolver->find("load")));
    ASSERT_TRUE(bool(resolver->find("import")));
    ASSERT_TRUE(bool(resolver->find("import_resolved")));
    ASSERT_TRUE(bool(resolver->find("resolve_json")));
    ASSERT_TRUE(bool(resolver->find("import_resolved_json")));
    ASSERT_TRUE(bool(resolver->find("__native")));

    auto parserMap = parser->find("map")->getValue(false, false);
    auto parserParseJson = parser->find("parse_json")->getValue(false, false);
    auto parserMaterializeJson = parser->find("materialize_json")->getValue(false, false);
    auto parserNative = parser->find("__native")->getValue(false, false);
    auto resolverLoad = resolver->find("load")->getValue(false, false);
    auto resolverImport = resolver->find("import")->getValue(false, false);
    auto resolverImportResolved = resolver->find("import_resolved")->getValue(false, false);
    auto resolverResolveJson = resolver->find("resolve_json")->getValue(false, false);
    auto resolverImportResolvedJson = resolver->find("import_resolved_json")->getValue(false, false);
    auto resolverNative = resolver->find("__native")->getValue(false, false);

    ASSERT_TRUE((bool)parserNative);
    ASSERT_TRUE((bool)resolverNative);
    ASSERT_TRUE(bool(parserNative->find("map")));
    ASSERT_TRUE(bool(parserNative->find("parse_json")));
    ASSERT_TRUE(bool(parserNative->find("materialize_json")));
    ASSERT_TRUE(bool(resolverNative->find("load")));
    ASSERT_TRUE(bool(resolverNative->find("import")));
    ASSERT_TRUE(bool(resolverNative->find("read_text")));
    ASSERT_TRUE(bool(resolverNative->find("request_id")));
    ASSERT_TRUE(bool(resolverNative->find("resolve_json")));
    ASSERT_TRUE(bool(resolverNative->find("import_resolved_json")));

    expectCodeFrameLongerThanBuiltinThunk(parserMap);
    expectCodeFrameLongerThanBuiltinThunk(parserParseJson);
    expectCodeFrameLongerThanBuiltinThunk(parserMaterializeJson);
    expectCodeFrameLongerThanBuiltinThunk(resolverLoad);
    expectCodeFrameLongerThanBuiltinThunk(resolverImport);
    expectCodeFrameLongerThanBuiltinThunk(resolverImportResolved);
    expectCodeFrameLongerThanBuiltinThunk(resolverResolveJson);
    expectCodeFrameLongerThanBuiltinThunk(resolverImportResolvedJson);

    bc = compiler.compile("import");
    state = vm.execute(bc);

    auto unresolved = vm.popData();
    ASSERT_TRUE((bool)unresolved);
    ASSERT_TRUE(unresolved->getData());
    EXPECT_EQ(std::string(static_cast<char*>(unresolved->getData()), unresolved->getLength()), "import");
}

TEST(CallbackTest, ImportBuiltinInjectsDefinitionsAndInvokesImmediately) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";
    const std::string headerPath = std::string(TEST_SOURCE_DIR) + "/libagentmath_poc.h";
    const std::string source =
        "[" + libPath + "] "
        "[" + headerPath + "] "
        "resolver.import ! @defs "
        "defs.add.safety "
        "10 32 defs.add !";

    BytecodeBuffer bc = compiler.compile(source);
    int state = vm.execute(bc);

    auto result = vm.popData();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(decodeStringInt(result), "42");

    auto safety = vm.popData();
    ASSERT_TRUE((bool)safety);
    EXPECT_EQ(std::string(static_cast<char*>(safety->getData()), safety->getLength()), "safe");
}

TEST(CallbackTest, InterpretedImportPipelineCanRoundTripThroughJson) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";
    const std::string headerPath = std::string(TEST_SOURCE_DIR) + "/libagentmath_poc.h";
    const std::string source =
        "[" + headerPath + "] parser.parse_json ! @schema "
        "[" + libPath + "] schema resolver.resolve_json ! @resolved "
        "resolved resolver.import_resolved_json ! @defs "
        "defs.add.safety "
        "10 32 defs.add !";

    BytecodeBuffer bc = compiler.compile(source);
    int state = vm.execute(bc);

    auto result = vm.popData();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(decodeStringInt(result), "42");

    auto safety = vm.popData();
    ASSERT_TRUE((bool)safety);
    EXPECT_EQ(std::string(static_cast<char*>(safety->getData()), safety->getLength()), "safe");

    BytecodeBuffer metadataCode = compiler.compile("defs.__cartographer.resolved_schema_format defs.__cartographer.resolved_schema_path");
    state = vm.execute(metadataCode);

    auto resolvedPath = vm.popData();
    auto resolvedFormat = vm.popData();
    ASSERT_TRUE((bool)resolvedFormat);
    ASSERT_TRUE((bool)resolvedPath);
    EXPECT_EQ(std::string(static_cast<char*>(resolvedFormat->getData()), resolvedFormat->getLength()), "resolver_json_v1");
    EXPECT_EQ(std::string(static_cast<char*>(resolvedPath->getData()), resolvedPath->getLength()), "<memory>");
}

TEST(CallbackTest, ParserMapCanRoundTripThroughJson) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";
    const std::string headerPath = std::string(TEST_SOURCE_DIR) + "/libagentmath_poc.h";
    const std::string source =
        "[" + libPath + "] resolver.load ! "
        "[" + headerPath + "] parser.parse_json ! parser.materialize_json ! @defs "
        "10 32 defs.add !";

    BytecodeBuffer bc = compiler.compile(source);
    int state = vm.execute(bc);

    auto result = vm.popData();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(decodeStringInt(result), "42");
}

TEST(CallbackTest, UnsafeImportedFunctionIsBlockedByDefault) {
    EdictVM vm;

    vm.pushData(agentc::createStringValue("echo blocked"));
    vm.pushData(make_imported_function_definition("system", "unsafe", 1));

    BytecodeBuffer bc;
    bc.addOp(VMOP_EVAL);
    int state = vm.execute(bc);

    ASSERT_TRUE(state & VM_ERROR);
    ASSERT_NE(vm.getError().find("Cartographer blocked unsafe import: system"), std::string::npos);
}

TEST(CallbackTest, SourceLevelUnsafeExtensionPolicyCanAllowAndReblock) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";
    const std::string headerPath = std::string(TEST_SOURCE_DIR) + "/libagentmath_poc.h";

    BytecodeBuffer importDefs = compiler.compile(
        "[" + libPath + "] "
        "[" + headerPath + "] "
        "resolver.import ! dup @defs");
    int state = vm.execute(importDefs);

    auto defs = vm.popData();
    ASSERT_TRUE((bool)defs);
    auto addItem = defs->find("add");
    ASSERT_TRUE(bool(addItem));
    auto addDef = addItem->getValue(false, false);
    ASSERT_TRUE((bool)addDef);
    agentc::addNamedItem(addDef, "safety", agentc::createStringValue("unsafe"));
    agentc::addNamedItem(addDef, "policy", agentc::createStringValue("blocked_by_default"));

    BytecodeBuffer allowStatus = compiler.compile("unsafe_extensions_allow unsafe_extensions_status");
    state = vm.execute(allowStatus);

    auto status = vm.popData();
    ASSERT_TRUE((bool)status);
    EXPECT_EQ(std::string(static_cast<char*>(status->getData()), status->getLength()), "allow");
    vm.popData();

    BytecodeBuffer callAllowed = compiler.compile("10 32 defs.add !");
    state = vm.execute(callAllowed);

    auto result = vm.popData();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(decodeStringInt(result), "42");

    BytecodeBuffer blockStatus = compiler.compile("unsafe_extensions_block");
    state = vm.execute(blockStatus);

    status = vm.popData();
    ASSERT_TRUE((bool)status);
    EXPECT_EQ(std::string(static_cast<char*>(status->getData()), status->getLength()), "block");

}

TEST(CallbackTest, DeferredImportStatusReturnsHandleSnapshot) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";
    const std::string headerPath = std::string(TEST_SOURCE_DIR) + "/libagentmath_poc.h";

    BytecodeBuffer queueImport = compiler.compile(
        "[" + libPath + "] "
        "[" + headerPath + "] "
        "resolver.import_deferred ! dup resolver.import_status !");
    int state = vm.execute(queueImport);

    auto statusHandle = vm.popData();
    ASSERT_TRUE((bool)statusHandle);
    auto queuedHandle = vm.popData();
    ASSERT_TRUE((bool)queuedHandle);

    auto queuedIdItem = queuedHandle->find("request_id");
    auto statusIdItem = statusHandle->find("request_id");
    ASSERT_TRUE(bool(queuedIdItem));
    ASSERT_TRUE(bool(statusIdItem));

    auto queuedId = queuedIdItem->getValue(false, false);
    auto statusId = statusIdItem->getValue(false, false);
    ASSERT_TRUE((bool)queuedId);
    ASSERT_TRUE((bool)statusId);
    EXPECT_EQ(std::string(static_cast<char*>(queuedId->getData()), queuedId->getLength()),
              std::string(static_cast<char*>(statusId->getData()), statusId->getLength()));

    auto boundaryItem = statusHandle->find("service_boundary");
    ASSERT_TRUE(bool(boundaryItem));
    auto boundary = boundaryItem->getValue(false, false);
    ASSERT_TRUE((bool)boundary);
    EXPECT_EQ(std::string(static_cast<char*>(boundary->getData()), boundary->getLength()), "subprocess_pipe");

    auto protocolItem = statusHandle->find("protocol");
    ASSERT_TRUE(bool(protocolItem));
    auto protocolValue = protocolItem->getValue(false, false);
    ASSERT_TRUE((bool)protocolValue);
    EXPECT_EQ(std::string(static_cast<char*>(protocolValue->getData()), protocolValue->getLength()), "protocol_v2");

    auto schemaFormatItem = statusHandle->find("api_schema_format");
    ASSERT_TRUE(bool(schemaFormatItem));
    auto schemaFormatValue = schemaFormatItem->getValue(false, false);
    ASSERT_TRUE((bool)schemaFormatValue);
    EXPECT_EQ(std::string(static_cast<char*>(schemaFormatValue->getData()), schemaFormatValue->getLength()), "parser_json_v1");

    auto statusItem = statusHandle->find("status");
    ASSERT_TRUE(bool(statusItem));
    auto statusValue = statusItem->getValue(false, false);
    ASSERT_TRUE((bool)statusValue);
    const std::string statusText(static_cast<char*>(statusValue->getData()), statusValue->getLength());
    ASSERT_TRUE(statusText == "queued" || statusText == "running" || statusText == "ready") << statusText;
}

TEST(CallbackTest, DeferredImportCollectCanUseHandleThroughRequestIdWrapper) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::string libPath = std::string(TEST_BUILD_DIR) + "/libagentmath_poc.so";
    const std::string headerPath = std::string(TEST_SOURCE_DIR) + "/libagentmath_poc.h";

    BytecodeBuffer queueImport = compiler.compile(
        "[" + libPath + "] "
        "[" + headerPath + "] "
        "resolver.import_deferred !");
    int state = vm.execute(queueImport);

    auto handle = vm.popData();
    ASSERT_TRUE((bool)handle);

    BytecodeBuffer statusCode = compiler.compile("resolver.import_status !");
    BytecodeBuffer collectCode = compiler.compile("resolver.import_collect !");

    CPtr<agentc::ListreeValue> latestStatus;
    bool ready = false;
    for (int attempt = 0; attempt < 200; ++attempt) {
        vm.pushData(handle);
        state = vm.execute(statusCode);

        latestStatus = vm.popData();
        ASSERT_TRUE((bool)latestStatus);

        auto statusItem = latestStatus->find("status");
        ASSERT_TRUE(bool(statusItem));
        auto statusValue = statusItem->getValue(false, false);
        ASSERT_TRUE((bool)statusValue);
        const std::string statusText(static_cast<char*>(statusValue->getData()), statusValue->getLength());
        if (statusText == "ready") {
            ready = true;
            break;
        }
        ASSERT_TRUE(statusText == "queued" || statusText == "running") << statusText;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(ready);

    vm.pushData(handle);
    state = vm.execute(collectCode);

    auto defs = vm.popData();
    ASSERT_TRUE((bool)defs);
    auto addItem = defs->find("add");
    ASSERT_TRUE(bool(addItem));
    auto addDef = addItem->getValue(false, false);
    ASSERT_TRUE((bool)addDef);

    auto metadataItem = defs->find("__cartographer");
    ASSERT_TRUE(bool(metadataItem));
    auto metadata = metadataItem->getValue(false, false);
    ASSERT_TRUE((bool)metadata);

    auto statusItem = metadata->find("status");
    ASSERT_TRUE(bool(statusItem));
    auto statusValue = statusItem->getValue(false, false);
    ASSERT_TRUE((bool)statusValue);
    EXPECT_EQ(std::string(static_cast<char*>(statusValue->getData()), statusValue->getLength()), "ready");
}

TEST(CallbackTest, ImportResolvedInjectsDefinitionsAndInvokesImmediately) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path libPath = buildDir / "libagentmath_poc.so";
    const std::filesystem::path headerPath = sourceDir / "libagentmath_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "edict_import_resolved_test.json";

    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) << error;

    agentc::cartographer::resolver::ResolvedApi resolved;
    ASSERT_TRUE(agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) << error;

    std::ofstream output(resolvedPath);
    ASSERT_TRUE(output.good());
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    output.close();

    BytecodeBuffer importCode = compiler.compile(
        "[" + resolvedPath.string() + "] "
        "resolver.import_resolved ! dup @defs 10 32 defs.add !");
    int state = vm.execute(importCode);

    auto result = vm.popData();
    ASSERT_TRUE((bool)result);
    EXPECT_EQ(decodeStringInt(result), "42");

    auto defs = vm.popData();
    ASSERT_TRUE((bool)defs);

    auto metaItem = defs->find("__cartographer");
    ASSERT_TRUE(bool(metaItem));
    auto metadata = metaItem->getValue(false, false);
    ASSERT_TRUE((bool)metadata);

    auto resolvedFormatItem = metadata->find("resolved_schema_format");
    ASSERT_TRUE(bool(resolvedFormatItem));
    auto resolvedFormat = resolvedFormatItem->getValue(false, false);
    ASSERT_TRUE((bool)resolvedFormat);
    EXPECT_EQ(std::string(static_cast<char*>(resolvedFormat->getData()), resolvedFormat->getLength()),
              "resolver_json_v1");

}

TEST(CallbackTest, ImportResolvedKanrenRuntimeAbiEvaluatesLogicSpec) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path rootBuildDir = buildDir.parent_path();
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path libPath = rootBuildDir / "kanren" / "libkanren.so";
    const std::filesystem::path headerPath = sourceDir / "kanren_runtime_ffi_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "edict_import_kanren_runtime_ffi.json";

    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) << error;

    agentc::cartographer::resolver::ResolvedApi resolved;
    ASSERT_TRUE(agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) << error;

    std::ofstream output(resolvedPath);
    ASSERT_TRUE(output.good());
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    output.close();

    BytecodeBuffer importCode = compiler.compile(
        "[" + resolvedPath.string() + "] "
        "resolver.import_resolved ! @logicffi "
        "{\"fresh\": [\"q\"], \"where\": [[\"membero\", \"q\", [\"tea\", \"cake\"]]], \"results\": [\"q\"]} logicffi.agentc_logic_eval_ltv ! @result "
        "result");
    int state = vm.execute(importCode);

    auto result = vm.popData();
    auto values = listToStrings(result);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], "tea");
    EXPECT_EQ(values[1], "cake");
}

TEST(CallbackTest, PureEdictWrappersBuildCanonicalLogicSpecForImportedKanren) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path rootBuildDir = buildDir.parent_path();
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path libPath = rootBuildDir / "kanren" / "libkanren.so";
    const std::filesystem::path headerPath = sourceDir / "kanren_runtime_ffi_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "edict_import_kanren_runtime_wrappers.json";

    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) << error;

    agentc::cartographer::resolver::ResolvedApi resolved;
    ASSERT_TRUE(agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) << error;

    std::ofstream output(resolvedPath);
    ASSERT_TRUE(output.good());
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    output.close();

    const std::string wrapperPrelude =
        "[" + resolvedPath.string() + "] "
        "resolver.import_resolved ! @logicffi "
        "[@rhs @lhs rhs lhs [] @items items ^] @pair "
        "[@x x [] @items items ^] @fresh "
        "[@x x [] @items items ^] @results "
        "[@rhs @lhs rhs lhs 'membero [] @goal goal ^] @membero "
        "[@results_list @goal_atom @fresh_list {\"fresh\": [], \"where\": [], \"results\": []} @spec "
        " fresh_list @spec.fresh "
        " goal_atom [] @where_clause where_clause ^ @spec.where "
        " results_list @spec.results "
        " spec] @logic_spec "
        "[logicffi.agentc_logic_eval_ltv !] @logic_eval ";

    std::stringstream input(wrapperPrelude);
    std::stringstream replOutput;
    // EdictREPL requires the initial cursor value (root)
    agentc::edict::EdictREPL repl(vm.getCursor().getValue(), input, replOutput);
    bool ok = repl.runScript(input);
    int state;
    ASSERT_TRUE(ok) << "Prelude failed to execute via REPL";

    state = vm.execute(compiler.compile("logic_spec(fresh(q) membero(q pair(tea cake)) results(q))"));

    auto spec = vm.popData();
    ASSERT_TRUE((bool)spec);

    auto freshRef = spec->find("fresh");
    ASSERT_TRUE((bool)freshRef);
    auto freshValue = freshRef->getValue(false, false);
    auto freshValues = listToStrings(freshValue);
    ASSERT_EQ(freshValues.size(), 1u);
    EXPECT_EQ(freshValues[0], "q");

    auto whereRef = spec->find("where");
    ASSERT_TRUE((bool)whereRef);
    auto whereClauses = listItems(whereRef->getValue(false, false));
    ASSERT_EQ(whereClauses.size(), 1u);
    auto goalItems = listItems(whereClauses[0]);
    ASSERT_EQ(goalItems.size(), 3u);
    EXPECT_EQ(decodeStringInt(goalItems[0]), "membero");
    EXPECT_EQ(decodeStringInt(goalItems[1]), "q");
    auto relationList = listToStrings(goalItems[2]);
    ASSERT_EQ(relationList.size(), 2u);
    EXPECT_EQ(relationList[0], "tea");
    EXPECT_EQ(relationList[1], "cake");

    auto resultsRef = spec->find("results");
    ASSERT_TRUE((bool)resultsRef);
    auto resultNames = listToStrings(resultsRef->getValue(false, false));
    ASSERT_EQ(resultNames.size(), 1u);
    EXPECT_EQ(resultNames[0], "q");

    vm.pushData(spec);
    state = vm.execute(compiler.compile("logic_eval !"));

    auto result = vm.popData();
    auto values = listToStrings(result);
    ASSERT_EQ(values.size(), 2u);
    EXPECT_EQ(values[0], "tea");
    EXPECT_EQ(values[1], "cake");
}

TEST(CallbackTest, ImportResolvedThreadRuntimeSpawnsThunkAndJoinsResult) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path libPath = buildDir / "libagentthreads_poc.so";
    const std::filesystem::path headerPath = sourceDir / "libagentthreads_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "edict_import_agentthreads_runtime.json";

    writeResolvedApi(libPath, headerPath, resolvedPath);

    int state = vm.execute(compiler.compile(
        "[" + resolvedPath.string() + "] resolver.import_resolved ! @threadffi threadffi"));

    auto defs = vm.popData();
    normalize_thread_runtime_defs(defs);
    const std::string source =
        "{\"return_type\": \"ltv\", \"children\": {\"p0\": {\"kind\": \"Parameter\", \"type\": \"ltv\"}}} "
        "[pop 'threaded-result] ffi_closure ! @worker pop "
        "worker 'seed threadffi.agentc_thread_spawn_ltv ! @handle pop "
        "handle threadffi.agentc_thread_join_ltv ! @result pop "
        "handle threadffi.agentc_thread_destroy ! "
        "result";

    state = vm.execute(compiler.compile(source));

    auto result = vm.popData();
    ASSERT_TRUE((bool)result);
    ASSERT_TRUE(result->getData());
    EXPECT_EQ(std::string(static_cast<const char*>(result->getData()), result->getLength()), "threaded-result");
}

TEST(CallbackTest, ImportResolvedThreadRuntimeSharedCellProvidesSnapshotIsolation) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path libPath = buildDir / "libagentthreads_poc.so";
    const std::filesystem::path headerPath = sourceDir / "libagentthreads_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "edict_import_agentthreads_snapshot.json";

    writeResolvedApi(libPath, headerPath, resolvedPath);

    int state = vm.execute(compiler.compile(
        "[" + resolvedPath.string() + "] resolver.import_resolved ! @threadffi threadffi"));

    auto defs = vm.popData();
    normalize_thread_runtime_defs(defs);

    const std::string source =
        "{} threadffi.agentc_shared_create_ltv ! @cell "
        "cell threadffi.agentc_shared_read_ltv ! @snapshot "
        "'local @snapshot.kind "
        "cell threadffi.agentc_shared_read_ltv ! @stored "
        "cell threadffi.agentc_shared_destroy ! "
        "stored";

    state = vm.execute(compiler.compile(source));

    auto stored = vm.popData();
    ASSERT_TRUE((bool)stored);
    auto kind = stored->find("kind");
    EXPECT_FALSE((bool)kind);
}

TEST(CallbackTest, ImportResolvedThreadRuntimeUpdatesSharedCellFromThread) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path libPath = buildDir / "libagentthreads_poc.so";
    const std::filesystem::path headerPath = sourceDir / "libagentthreads_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "edict_import_agentthreads_shared_write.json";

    writeResolvedApi(libPath, headerPath, resolvedPath);

    int state = vm.execute(compiler.compile(
        "[" + resolvedPath.string() + "] resolver.import_resolved ! @threadffi threadffi"));

    auto defs = vm.popData();
    normalize_thread_runtime_defs(defs);
    const std::string source =
        "{\"status\": \"initial\"} threadffi.agentc_shared_create_ltv ! @cell pop "
        "{\"return_type\": \"ltv\", \"children\": {\"p0\": {\"kind\": \"Parameter\", \"type\": \"ltv\"}}} "
        "[cell {\"status\": \"threaded\"} threadffi.agentc_shared_write_ltv ! pop 'updated] ffi_closure ! @worker pop "
        "worker 'seed threadffi.agentc_thread_spawn_ltv ! @handle pop "
        "handle threadffi.agentc_thread_join_ltv ! pop "
        "cell threadffi.agentc_shared_read_ltv ! @stored pop "
        "handle threadffi.agentc_thread_destroy ! "
        "cell threadffi.agentc_shared_destroy ! "
        "stored";

    state = vm.execute(compiler.compile(source));

    auto stored = vm.popData();
    ASSERT_TRUE((bool)stored);
    auto status = stored->find("status");
    ASSERT_TRUE((bool)status);
    auto value = status->getValue(false, false);
    ASSERT_TRUE((bool)value);
    ASSERT_TRUE(value->getData());
    EXPECT_EQ(std::string(static_cast<const char*>(value->getData()), value->getLength()), "threaded");
}

TEST(CallbackTest, ImportResolvedThreadRuntimeDirectCapturedMutationDoesNotLeakAcrossThreads) {
    EdictVM vm;
    EdictCompiler compiler;

    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path libPath = buildDir / "libagentthreads_poc.so";
    const std::filesystem::path headerPath = sourceDir / "libagentthreads_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "edict_import_agentthreads_direct_mutation.json";

    writeResolvedApi(libPath, headerPath, resolvedPath);

    int state = vm.execute(compiler.compile(
        "[" + resolvedPath.string() + "] resolver.import_resolved ! @threadffi threadffi"));

    auto defs = vm.popData();
    normalize_thread_runtime_defs(defs);
    const std::string source =
        "[] @session pop "
        "{\"return_type\": \"ltv\", \"children\": {\"p0\": {\"kind\": \"Parameter\", \"type\": \"ltv\"}}} "
        "[session 'threaded @mode pop 'done] ffi_closure ! @worker pop "
        "worker 'seed threadffi.agentc_thread_spawn_ltv ! @handle pop "
        "handle threadffi.agentc_thread_join_ltv ! pop "
        "handle threadffi.agentc_thread_destroy ! "
        "session mode";

    state = vm.execute(compiler.compile(source));

    auto mode = vm.popData();
    ASSERT_TRUE((bool)mode);
    EXPECT_EQ(std::string(static_cast<const char*>(mode->getData()), mode->getLength()), "mode");
}

TEST(CallbackTest, EdictCliImportResolvedDemoPrintsExpectedResult) {
    const std::filesystem::path edictBinDir(TEST_EDICT_BIN_DIR);
    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);
    const std::filesystem::path edictPath = edictBinDir / "edict";
    const std::filesystem::path libPath = buildDir / "libagentmath_poc.so";
    const std::filesystem::path headerPath = sourceDir / "libagentmath_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "edict_cli_import_resolved_demo.json";

    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    ASSERT_TRUE(agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) << error;

    agentc::cartographer::resolver::ResolvedApi resolved;
    ASSERT_TRUE(agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) << error;

    std::ofstream output(resolvedPath);
    ASSERT_TRUE(output.good());
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    output.close();

    const std::string source =
        "[" + resolvedPath.string() + "] "
        "resolver.import_resolved ! dup @defs 10 32 defs.add !";

    ProcessResult result = runProcess(makeArgList({edictPath.string(), "-e", source}));
    ASSERT_EQ(result.exitCode, 0) << result.stderrText;
    EXPECT_TRUE(result.stderrText.empty());
    EXPECT_NE(result.stdoutText.find("stack size: 2"), std::string::npos);
    EXPECT_NE(result.stdoutText.find("0: 42"), std::string::npos);
}
