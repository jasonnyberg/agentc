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

#include <iostream>
#include <filesystem>
#include <fstream>

#include "../cartographer/parser.h"
#include "../cartographer/resolver.h"
#include "../edict/edict_compiler.h"
#include "../edict/edict_vm.h"

using namespace agentc::edict;

namespace {

bool runScript(EdictVM& vm, const std::string& script) {
    auto code = EdictCompiler().compile(script);
    return (vm.execute(code) & VM_ERROR) == 0;
}

} // namespace

int main() {
    EdictVM vm;

    const std::filesystem::path buildDir = std::filesystem::current_path();
    const std::filesystem::path rootDir = buildDir.parent_path();
    const std::filesystem::path libPath = buildDir / "kanren" / "libkanren.so";
    const std::filesystem::path headerPath = rootDir / "cartographer" / "tests" / "kanren_runtime_ffi_poc.h";
    const std::filesystem::path resolvedPath = buildDir / "demo_cognitive_validation_kanren_runtime_ffi.json";

    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    if (!agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    agentc::cartographer::resolver::ResolvedApi resolved;
    if (!agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    std::ofstream output(resolvedPath);
    if (!output.good()) {
        std::cerr << "failed to write resolved kanren import json\n";
        return 1;
    }
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    output.close();

    if (!runScript(vm, "[" + resolvedPath.string() + "] resolver.import_resolved ! @logicffi logicffi.agentc_logic_eval_ltv @logic logic @logic_run")) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }

    if (!runScript(vm, R"(
        {"pattern": ["dup", "dot", "sqrt"], "replacement": ["magnitude"]}
        rewrite_define ! /
    )")) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }
    vm.popData();

    const size_t baseline = vm.getStackSize();
    const size_t baselineRules = vm.getRewriteRuleCount();
    auto checkpoint = vm.beginTransaction();
    if (!checkpoint.valid) {
        std::cerr << "failed to start cognitive validation transaction\n";
        return 1;
    }

    if (!runScript(vm, R"(
        {"fresh": ["q"],
         "where": [["membero", "q", ["tea", "cake"]]],
         "results": ["q"]}
        logic!
    )")) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }

    auto logicAnswers = vm.popData();
    size_t answerCount = 0;
    if (logicAnswers && logicAnswers->isListMode()) {
        logicAnswers->forEachList([&](CPtr<agentc::ListreeValueRef>&) { ++answerCount; });
    }

    if (!runScript(vm, R"(
        "dup" "dot" "sqrt"
    )")) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }

    auto rewriteResult = vm.popData();
    std::string rewriteText;
    if (rewriteResult && rewriteResult->getData()) {
        rewriteText.assign(static_cast<char*>(rewriteResult->getData()), rewriteResult->getLength());
    }

    std::cout << "logic answers inside transaction: " << answerCount << "\n";
    std::cout << "rewrite result inside transaction: " << rewriteText << "\n";

    if (!runScript(vm, R"(
        {"pattern": ["temp"], "replacement": ["transient"]}
        rewrite_define ! /
    )")) {
        std::cerr << vm.getError() << "\n";
        return 1;
    }
    vm.popData();
    std::cout << "rewrite rules inside transaction: " << vm.getRewriteRuleCount() << "\n";

    if (!vm.rollbackTransaction(checkpoint)) {
        std::cerr << "failed to rollback cognitive validation transaction\n";
        return 1;
    }

    std::cout << "stack size after rollback: " << vm.getStackSize() << " (baseline " << baseline << ")\n";
    std::cout << "rewrite rules after rollback: " << vm.getRewriteRuleCount() << " (baseline " << baselineRules << ")\n";
    return vm.getStackSize() == baseline && vm.getRewriteRuleCount() == baselineRules ? 0 : 1;
}
