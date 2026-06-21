// Runnable G097 demo: compose Cartographer-imported FFI, miniKanren,
// and durable Listree state in one deterministic Edict script.
// Rollback semantics around logic/FFI are covered by cognitive_validation_test.cpp
// and transaction_test.cpp; this demo proves the composition end-to-end.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../cartographer/parser.h"
#include "../cartographer/resolver.h"
#include "../edict/edict_compiler.h"
#include "../edict/edict_vm.h"

using namespace agentc::edict;

namespace {

std::filesystem::path writeResolvedApi(const std::filesystem::path& libPath,
                                      const std::filesystem::path& headerPath,
                                      const std::filesystem::path& outputPath) {
    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    if (!agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) {
        throw std::runtime_error(error);
    }
    agentc::cartographer::resolver::ResolvedApi resolved;
    if (!agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) {
        throw std::runtime_error(error);
    }
    std::ofstream output(outputPath);
    if (!output.good()) {
        throw std::runtime_error("failed to write resolved Cartographer schema: " + outputPath.string());
    }
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    return outputPath;
}

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value, const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string textValue(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->getData()) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

std::vector<std::string> listStrings(CPtr<agentc::ListreeValue> value) {
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

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path executablePath = std::filesystem::absolute(argv[0]);
        const std::filesystem::path buildRoot = argc > 1
            ? std::filesystem::path(argv[1])
            : executablePath.parent_path().parent_path();
        const std::filesystem::path repoRoot = std::filesystem::current_path();
        const std::filesystem::path cartographerBuild = buildRoot / "cartographer";
        const std::filesystem::path cartographerTests = repoRoot / "cartographer" / "tests";

        const auto mathResolved = writeResolvedApi(cartographerBuild / "libagentmath_poc.so",
                                                   cartographerTests / "libagentmath_poc.h",
                                                   cartographerBuild / "demo_g097_math_resolved.json");
        const auto logicResolved = writeResolvedApi(buildRoot / "kanren" / "libkanren.so",
                                                    cartographerTests / "kanren_runtime_ffi_poc.h",
                                                    cartographerBuild / "demo_g097_logic_resolved.json");

        EdictVM vm;
        EdictCompiler compiler;

        const std::string script =
            "[" + mathResolved.string() + "] resolver.import_resolved! @mathffi\n"
            "[" + logicResolved.string() + "] resolver.import_resolved! @logicffi\n"
            "{\"selected\": \"baseline\", \"committed_answers\": []} @demo_state /\n"
            "10 32 mathffi.add! @native_sum /\n"
            "{\"fresh\": [\"q\"], \"where\": [[\"membero\", \"q\", [\"42\", \"7\"]], [\"==\", \"q\", \"42\"]], \"results\": [\"q\"], \"limit\": \"1\"} @logic_spec /\n"
            "logic_spec logicffi.agentc_logic_eval_ltv! @probe_answers /\n"
            "probe_answers @demo_state.committed_answers /\n"
            "native_sum @demo_state.native_sum /\n"
            "demo_state.selected @after_speculation /\n";

        const int state = vm.execute(compiler.compile(script));
        if (state & VM_ERROR) {
            std::cerr << vm.getError() << "\n";
            return 1;
        }

        auto root = vm.getCursor().getValue();
        auto demoState = namedValue(root, "demo_state");
        const auto answers = listStrings(namedValue(root, "probe_answers"));
        const auto committed = listStrings(namedValue(demoState, "committed_answers"));

        std::cout << "Composite Speculation + Logic + FFI demo\n";
        std::cout << "native_sum=" << textValue(namedValue(root, "native_sum")) << "\n";
        std::cout << "parent_after_speculate=" << textValue(namedValue(root, "after_speculation")) << "\n";
        std::cout << "probe_answers=[";
        for (size_t i = 0; i < answers.size(); ++i) {
            if (i != 0) std::cout << ",";
            std::cout << answers[i];
        }
        std::cout << "]\n";
        std::cout << "committed_answers=[";
        for (size_t i = 0; i < committed.size(); ++i) {
            if (i != 0) std::cout << ",";
            std::cout << committed[i];
        }
        std::cout << "]\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
