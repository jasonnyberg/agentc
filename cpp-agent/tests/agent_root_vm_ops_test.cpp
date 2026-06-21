#include <gtest/gtest.h>


#include "../runtime/persistence/agent_root_vm_ops.h"
#include "../../edict/edict_compiler.h"
#include "../../edict/edict_vm.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#ifndef TEST_SOURCE_DIR
#define TEST_SOURCE_DIR "."
#endif
#ifndef TEST_BUILD_DIR
#define TEST_BUILD_DIR "."
#endif

namespace {

std::unique_ptr<agentc::edict::EdictVM> makeVm(const nlohmann::json& rootJson) {
    auto rootValue = agentc::fromJson(rootJson.dump());
    EXPECT_TRUE(rootValue);
    auto vm = std::make_unique<agentc::edict::EdictVM>(rootValue);

    auto canonicalRoot = agentc::fromJson(rootJson.dump());
    EXPECT_TRUE(canonicalRoot);
    vm->setCursor(canonicalRoot);
    vm->reset();
    return vm;
}

bool jsonBoolish(const nlohmann::json& value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_string()) {
        return value.get<std::string>() == "true";
    }
    return false;
}

agentc::runtime::VmRuntimeImportArtifacts mockRuntimeArtifacts() {
    const std::filesystem::path sourceRoot(TEST_SOURCE_DIR);
    const std::filesystem::path buildRoot(TEST_BUILD_DIR);
    return agentc::runtime::VmRuntimeImportArtifacts{
        .extensions_library_path = (buildRoot / "extensions" / "libagentc_extensions.so").string(),
        .extensions_header_path = (sourceRoot / "extensions" / "agentc_stdlib.h").string(),
        .runtime_library_path = (buildRoot / "cpp-agent" / "libagent_runtime_mock.so").string(),
        .runtime_header_path = (sourceRoot / "cpp-agent" / "include" / "agentc_runtime" / "agentc_runtime.h").string(),
        .agentc_module_path = (sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc.edict").string(),
        .agentc_stateful_loop_module_path = (sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc_stateful_loop.edict").string(),
        .agentc_provider_contracts_module_path = (sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc_provider_contracts.edict").string(),
        .llm_module_path = (sourceRoot / "cpp-agent" / "edict" / "modules" / "llm.edict").string(),
        .agentc_agent_root_module_path = (sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc_agent_root.edict").string()
    };
}


} // namespace

TEST(AgentRootVmOpsTest, ScriptCanConsumeValuesPushedFromCpp) {
    agentc::edict::EdictVM vm;
    vm.pushData(agentc::createStringValue("first"));
    vm.pushData(agentc::createStringValue("second"));

    auto code = agentc::edict::EdictCompiler().compile("@b @a a b");
    ASSERT_FALSE(vm.execute(code) & agentc::edict::VM_ERROR) << vm.getError();

    auto second = vm.popData();
    auto first = vm.popData();
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(std::string(static_cast<char*>(first->getData()), first->getLength()), "first");
    EXPECT_EQ(std::string(static_cast<char*>(second->getData()), second->getLength()), "second");
}

TEST(AgentRootVmOpsTest, ScriptCanBuildObjectViaPathAssignment) {
    agentc::edict::EdictVM vm;
    vm.pushData(agentc::createStringValue("value-x"));

    auto code = agentc::edict::EdictCompiler().compile("@value {\"a\": \"\"} @obj value @obj.a obj");
    ASSERT_FALSE(vm.execute(code) & agentc::edict::VM_ERROR) << vm.getError();

    auto result = nlohmann::json::parse(agentc::toJson(vm.popData()));
    EXPECT_EQ(result["a"].get<std::string>(), "value-x");
}

TEST(AgentRootVmOpsTest, ScriptCanReadNestedPathFromPushedRootValue) {
    agentc::edict::EdictVM vm;
    auto root = agentc::fromJson(R"({"conversation":{"system_prompt":"system prompt"}})");
    ASSERT_TRUE(root);
    vm.pushData(root);

    auto code = agentc::edict::EdictCompiler().compile("@root root.conversation.system_prompt");
    ASSERT_FALSE(vm.execute(code) & agentc::edict::VM_ERROR) << vm.getError();

    auto result = vm.popData();
    ASSERT_TRUE(result);
    EXPECT_EQ(std::string(static_cast<char*>(result->getData()), result->getLength()), "system prompt");
}

TEST(AgentRootVmOpsTest, ScriptCanBuildRequestShapeFromPromptAndRoot) {
    agentc::edict::EdictVM vm;
    auto root = agentc::fromJson(R"({"conversation":{"system_prompt":"system prompt","messages":[{"role":"user","text":"hello"}]}})");
    ASSERT_TRUE(root);
    vm.pushData(root);
    vm.pushData(agentc::createStringValue("next question"));

    auto code = agentc::edict::EdictCompiler().compile(R"(@prompt @root {"system": "", "messages": [], "prompt": "", "response_mode": "text"} @request root.conversation.system_prompt @request.system root.conversation.messages @request.messages prompt @request.prompt [text] @request.response_mode request)");
    ASSERT_FALSE(vm.execute(code) & agentc::edict::VM_ERROR) << vm.getError();

    auto result = nlohmann::json::parse(agentc::toJson(vm.popData()));
    EXPECT_EQ(result["system"].get<std::string>(), "system prompt");
    EXPECT_EQ(result["prompt"].get<std::string>(), "next question");
    EXPECT_EQ(result["response_mode"].get<std::string>(), "text");
    ASSERT_EQ(result["messages"].size(), 1u);
}


TEST(AgentRootVmOpsTest, RehydratesTransientRuntimeStateExplicitlyOnStartup) {
    auto root = agentc::runtime::make_default_agent_root("system", "google", "gemini-3.1-pro-preview");
    root["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "hello"}},
        nlohmann::json{{"role", "assistant"}, {"text", "world"}}
    });
    root["__vm_runtime_response"] = nlohmann::json{{"stale", true}};
    root["vm_runtime_handle"] = "stale-handle";

    auto vm = makeVm(root);
    const auto artifacts = mockRuntimeArtifacts();
    agentc::runtime::rehydrate_vm_runtime_state(
        *vm,
        nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}},
        artifacts,
        "startup-restored",
        "agentc-config.json");

    const auto rehydrated = nlohmann::json::parse(agentc::toJson(vm->getCursor().getValue()));
    ASSERT_TRUE(rehydrated["runtime"].is_object());
    ASSERT_TRUE(rehydrated["runtime"]["rehydration"].is_object());
    EXPECT_EQ(rehydrated["runtime"]["rehydration"]["status"].get<std::string>(), "complete");
    EXPECT_EQ(rehydrated["runtime"]["rehydration"]["last_event"].get<std::string>(), "startup-restored");
    EXPECT_EQ(rehydrated["runtime"]["rehydration"]["config_source"].get<std::string>(), "config-file");
    EXPECT_EQ(rehydrated["runtime"]["rehydration"]["config_path_hint"].get<std::string>(), "agentc-config.json");
    EXPECT_EQ(rehydrated["runtime"]["rehydration"]["transient_handles_persisted"].get<std::string>(),
              "not-persisted");
    EXPECT_EQ(rehydrated["runtime"]["rehydration"]["transient_state_rebuilt"].get<std::string>(),
              "rebuilt");
    EXPECT_EQ(rehydrated["runtime"]["rehydration"]["binding"]["kind"].get<std::string>(),
              "embedded_imported_agentc_runtime");
    EXPECT_EQ(rehydrated["runtime"]["rehydration"]["binding"]["module_name"].get<std::string>(), "agentc");
    EXPECT_EQ(rehydrated["runtime"]["default_provider"].get<std::string>(), "google");
    EXPECT_EQ(rehydrated["runtime"]["default_model"].get<std::string>(), "gemini-3.1-pro-preview");
    EXPECT_FALSE(rehydrated.contains("__vm_runtime_response"));
    EXPECT_FALSE(rehydrated.contains("vm_runtime_handle"));
    ASSERT_EQ(rehydrated["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(rehydrated["conversation"]["messages"][1]["text"].get<std::string>(), "world");
}

TEST(AgentRootVmOpsTest, RehydrateFillsMissingRuntimeDefaultsFromEdictCatalog) {
    nlohmann::json root = {
        {"conversation", {
            {"system_prompt", "system"},
            {"messages", nlohmann::json::array()},
            {"assistant_text", ""}
        }},
        {"runtime", nlohmann::json::object()},
        {"loop", {{"status", "ready"}}}
    };

    auto vm = makeVm(root);
    agentc::runtime::rehydrate_vm_runtime_state(
        *vm,
        nlohmann::json::object(),
        mockRuntimeArtifacts(),
        "startup-fresh",
        "");

    const auto rehydrated = nlohmann::json::parse(agentc::toJson(vm->getCursor().getValue()));
    ASSERT_TRUE(rehydrated["runtime"].is_object());
    EXPECT_EQ(rehydrated["runtime"]["default_provider"].get<std::string>(), "google");
    EXPECT_EQ(rehydrated["runtime"]["default_model"].get<std::string>(), "gemini-3.1-pro-preview");
    ASSERT_TRUE(rehydrated["runtime"]["provider_contract"].is_object());
    EXPECT_EQ(rehydrated["runtime"]["provider_contract"]["id"].get<std::string>(), "google");
    EXPECT_EQ(rehydrated["runtime"]["provider_contract"]["transport_api"].get<std::string>(),
              "google-gemini-cli");
}

TEST(AgentRootVmOpsTest, RunTurnCanInvokeRuntimeThroughImportedVmBindings) {
    auto root = agentc::runtime::make_default_agent_root("system", "google", "gemini-3.1-pro-preview");
    root["runtime"]["default_provider"] = "mock";
    root["runtime"]["default_model"] = "mock-model";
    auto vm = makeVm(root);

    agentc::runtime::rehydrate_vm_runtime_state(
        *vm,
        nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}},
        mockRuntimeArtifacts(),
        "startup-fresh",
        "");
        
    agentc::runtime::run_vm_agent_turn_native(*vm, "Say hello through import");

    const auto persisted = nlohmann::json::parse(agentc::toJson(vm->getCursor().getValue()));
    const auto response = persisted["conversation"]["last_response"];
    
    EXPECT_TRUE(jsonBoolish(response["ok"]));
    EXPECT_EQ(response["message"]["text"].get<std::string>(), "mock:Say hello through import");
    EXPECT_EQ(agentc::runtime::reply_text_from_vm_agent_root(*vm), "mock:Say hello through import");
    ASSERT_EQ(persisted["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(persisted["conversation"]["messages"][1]["text"].get<std::string>(), "mock:Say hello through import");
}
