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

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../../cartographer/parser.h"
#include "../../cartographer/resolver.h"
#include "../edict_compiler.h"
#include "../edict_vm.h"

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif
#ifndef TEST_EDICT_SOURCE_DIR
#define TEST_EDICT_SOURCE_DIR "."
#endif
#ifndef TEST_ROOT_BUILD_DIR
#define TEST_ROOT_BUILD_DIR "."
#endif

using agentc::ListreeValue;
using agentc::edict::EdictCompiler;
using agentc::edict::EdictVM;
using namespace agentc::edict;

namespace {

CPtr<ListreeValue> namedValue(CPtr<ListreeValue> value, const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string textValue(CPtr<ListreeValue> value) {
    if (!value || !value->getData()) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

std::vector<CPtr<ListreeValue>> listItems(CPtr<ListreeValue> value) {
    std::vector<CPtr<ListreeValue>> out;
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

std::vector<std::string> listStrings(CPtr<ListreeValue> value) {
    std::vector<std::string> out;
    for (const auto& item : listItems(value)) {
        out.push_back(textValue(item));
    }
    return out;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string cognitiveModuleText() {
    const auto edictSourceDir = std::filesystem::path(TEST_EDICT_SOURCE_DIR);
    const auto repoRoot = edictSourceDir.parent_path();
    return readTextFile(repoRoot / "cpp-agent" / "edict" / "modules" / "cognitive.edict") + "\n";
}

void executeScript(EdictVM& vm, const std::string& script) {
    EdictCompiler compiler;
    const int state = vm.execute(compiler.compile(script));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();
}

void loadCognitiveModule(EdictVM& vm) {
    executeScript(vm, cognitiveModuleText());
}

std::filesystem::path writeResolvedApi(const std::filesystem::path& libPath,
                                      const std::filesystem::path& headerPath,
                                      const std::filesystem::path& outputPath) {
    agentc::cartographer::Mapper mapper;
    agentc::cartographer::Mapper::ParseDescription description;
    std::string error;
    EXPECT_TRUE(agentc::cartographer::parser::parseHeaderToDescription(mapper, headerPath.string(), description, error)) << error;

    agentc::cartographer::resolver::ResolvedApi resolved;
    EXPECT_TRUE(agentc::cartographer::resolver::resolveApiDescription(libPath.string(), description, resolved, error)) << error;

    std::ofstream output(outputPath);
    EXPECT_TRUE(output.good());
    output << agentc::cartographer::resolver::encodeResolvedApi(resolved);
    output.close();
    return outputPath;
}

std::string compositeDemoPrelude() {
    const std::filesystem::path buildDir(TEST_BUILD_DIR);
    const std::filesystem::path rootBuildDir(TEST_ROOT_BUILD_DIR);
    const std::filesystem::path sourceDir(TEST_SOURCE_DIR);

    const auto mathResolved = writeResolvedApi(buildDir / "libagentmath_poc.so",
                                               sourceDir / "libagentmath_poc.h",
                                               buildDir / "g097_math_resolved.json");
    const auto logicResolved = writeResolvedApi(rootBuildDir / "kanren" / "libkanren.so",
                                                sourceDir / "kanren_runtime_ffi_poc.h",
                                                buildDir / "g097_logic_resolved.json");

    return std::string("[") + mathResolved.string() + "] resolver.import_resolved! @mathffi\n" +
           "[" + logicResolved.string() + "] resolver.import_resolved! @logicffi\n";
}

} // namespace

TEST(CognitiveScaffoldTest, InvestigationScaffoldPersistsAndSerializesAcrossTurns) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);
    loadCognitiveModule(vm);

    executeScript(vm, R"(
        "parser-regression" cognitive.investigation_new! @investigation
    )");
    executeScript(vm, R"(
        investigation "H1" "cache-stale" cognitive.investigation_add_hypothesis! @investigation
        investigation "H1" "edict/tests/vm_stack_tests.cpp:136" cognitive.investigation_add_evidence! @investigation
        investigation to_json! @investigation_snapshot
    )");

    auto investigation = namedValue(root, "investigation");
    ASSERT_TRUE(investigation);
    EXPECT_EQ(textValue(namedValue(investigation, "kind")), "cognitive_scaffold");
    EXPECT_EQ(textValue(namedValue(investigation, "scaffold")), "investigation");
    EXPECT_EQ(textValue(namedValue(investigation, "goal")), "parser-regression");

    auto hypotheses = listItems(namedValue(investigation, "hypotheses"));
    ASSERT_EQ(hypotheses.size(), 1u);
    EXPECT_EQ(textValue(namedValue(hypotheses[0], "id")), "H1");
    EXPECT_EQ(textValue(namedValue(hypotheses[0], "claim")), "cache-stale");

    auto findings = listItems(namedValue(investigation, "findings"));
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(textValue(namedValue(findings[0], "hypothesis_id")), "H1");
    EXPECT_EQ(textValue(namedValue(findings[0], "evidence")), "edict/tests/vm_stack_tests.cpp:136");

    const std::string snapshot = textValue(namedValue(root, "investigation_snapshot"));
    EXPECT_NE(snapshot.find("parser-regression"), std::string::npos);
    EXPECT_NE(snapshot.find("cache-stale"), std::string::npos);
}

TEST(CognitiveScaffoldTest, SnapshotRestoreIsolatesBranchFromParentState) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);
    loadCognitiveModule(vm);

    executeScript(vm, R"(
        "branch-safety" cognitive.investigation_new! @investigation
        investigation "H1" "baseline" cognitive.investigation_add_hypothesis! @investigation
        investigation to_json! @parent_snapshot
    )");

    auto investigation = namedValue(root, "investigation");
    ASSERT_TRUE(investigation);
    auto parentHypotheses = listItems(namedValue(investigation, "hypotheses"));
    ASSERT_EQ(parentHypotheses.size(), 1u);
    EXPECT_EQ(textValue(namedValue(parentHypotheses[0], "id")), "H1");

    executeScript(vm, R"(
        investigation "H2" "branch-only" cognitive.investigation_add_hypothesis! @investigation
        investigation to_json! @branch_snapshot
    )");

    auto branchInvestigation = namedValue(root, "investigation");
    ASSERT_TRUE(branchInvestigation);
    auto branchHypotheses = listItems(namedValue(branchInvestigation, "hypotheses"));
    ASSERT_EQ(branchHypotheses.size(), 2u);
    EXPECT_EQ(textValue(namedValue(branchHypotheses[1], "id")), "H2");

    const std::string branchSnapshot = textValue(namedValue(root, "branch_snapshot"));
    EXPECT_NE(branchSnapshot.find("branch-only"), std::string::npos);

    executeScript(vm, R"(
        parent_snapshot from_json! @investigation
        investigation to_json! @restored_snapshot
    )");

    auto restored = namedValue(root, "investigation");
    ASSERT_TRUE(restored);
    auto restoredHypotheses = listItems(namedValue(restored, "hypotheses"));
    ASSERT_EQ(restoredHypotheses.size(), 1u);
    EXPECT_EQ(textValue(namedValue(restoredHypotheses[0], "id")), "H1");

    const std::string restoredSnapshot = textValue(namedValue(root, "restored_snapshot"));
    EXPECT_EQ(restoredSnapshot.find("branch-only"), std::string::npos);
}

TEST(CognitiveScaffoldTest, CompanionReviewAndRefactorShapesAreInspectable) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);
    loadCognitiveModule(vm);

    executeScript(vm, R"(
        "edict-module-review" cognitive.code_review_new! @review
        "cognitive-module-refactor" cognitive.refactor_plan_new! @plan
        plan "S1" "split-parser" "edict/parser.cpp" "edict_tests" cognitive.refactor_plan_add_step! @plan
    )");

    auto review = namedValue(root, "review");
    ASSERT_TRUE(review);
    EXPECT_EQ(textValue(namedValue(review, "scaffold")), "code_review");
    EXPECT_FALSE(listStrings(namedValue(review, "success_criteria")).empty());

    auto plan = namedValue(root, "plan");
    ASSERT_TRUE(plan);
    EXPECT_EQ(textValue(namedValue(plan, "scaffold")), "refactor_plan");
    auto steps = listItems(namedValue(plan, "steps"));
    ASSERT_EQ(steps.size(), 1u);
    EXPECT_EQ(textValue(namedValue(steps[0], "id")), "S1");
    EXPECT_EQ(textValue(namedValue(steps[0], "verify")), "edict_tests");
}

TEST(CompositeDemoTest, FfiLogicAndCommitComposeDeterministically) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);
    executeScript(vm, compositeDemoPrelude());

    executeScript(vm, R"(
        {"selected": "baseline", "committed_answers": []} @demo_state /
        10 32 mathffi.add! @native_sum /
        {"fresh": ["q"], "where": [["membero", "q", ["42", "7"]], ["==", "q", "42"]], "results": ["q"], "limit": "1"} @logic_spec /
    )");

    EXPECT_EQ(textValue(namedValue(root, "native_sum")), "42");

    executeScript(vm, R"(
        logic_spec logicffi.agentc_logic_eval_ltv! @probe_answers /
        probe_answers @demo_state.committed_answers /
        native_sum @demo_state.native_sum /
        demo_state.selected @after_speculation /
        demo_state to_json! @demo_json
    )");

    EXPECT_EQ(textValue(namedValue(root, "after_speculation")), "baseline");

    auto probeAnswers = listStrings(namedValue(root, "probe_answers"));
    ASSERT_EQ(probeAnswers.size(), 1u);
    EXPECT_EQ(probeAnswers[0], "42");

    auto demoState = namedValue(root, "demo_state");
    ASSERT_TRUE(demoState);
    EXPECT_EQ(textValue(namedValue(demoState, "selected")), "baseline");
    EXPECT_EQ(textValue(namedValue(demoState, "native_sum")), "42");
    auto committedAnswers = listStrings(namedValue(demoState, "committed_answers"));
    ASSERT_EQ(committedAnswers.size(), 1u);
    EXPECT_EQ(committedAnswers[0], "42");

    const std::string demoJson = textValue(namedValue(root, "demo_json"));
    EXPECT_NE(demoJson.find("baseline"), std::string::npos);
    EXPECT_NE(demoJson.find("42"), std::string::npos);
}
