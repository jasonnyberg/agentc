#include <gtest/gtest.h>

#include "../runtime/persistence/agent_root_state.h"
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

TEST(AgentRootVmOpsTest, BuildsRuntimeRequestInsideEmbeddedVm) {
    auto root = agentc::runtime::make_default_agent_root("system prompt", "google", "gemini-2.5-flash");
    root["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "hello"}},
        nlohmann::json{{"role", "assistant"}, {"text", "world"}}
    });

    auto vm = makeVm(root);
    const auto request = agentc::runtime::build_request_from_vm_agent_root(*vm, "next question");

    EXPECT_EQ(request["system"].get<std::string>(), "system prompt");
    ASSERT_EQ(request["messages"].size(), 2u);
    EXPECT_EQ(request["messages"][1]["text"].get<std::string>(), "world");
    EXPECT_EQ(request["prompt"].get<std::string>(), "next question");
    EXPECT_EQ(request["response_mode"].get<std::string>(), "text");

    const auto unchanged = nlohmann::json::parse(agentc::toJson(vm->getCursor().getValue()));
    ASSERT_EQ(unchanged["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(unchanged["loop"]["status"].get<std::string>(), "ready");
}

TEST(AgentRootVmOpsTest, AppliesRuntimeResponseInsideEmbeddedVmAndUpdatesRoot) {
    auto vm = makeVm(agentc::runtime::make_default_agent_root("system", "google", "gemini-2.5-flash"));
    const nlohmann::json response = {
        {"ok", true},
        {"message", {
            {"role", "assistant"},
            {"text", "Hello from VM"}
        }}
    };

    agentc::runtime::apply_runtime_response_to_vm_agent_root(*vm, "Say hello", response);

    const auto root = nlohmann::json::parse(agentc::toJson(vm->getCursor().getValue()));
    EXPECT_EQ(root["conversation"]["last_prompt"].get<std::string>(), "Say hello");
    EXPECT_EQ(root["conversation"]["assistant_text"].get<std::string>(), "Hello from VM");
    EXPECT_TRUE(jsonBoolish(root["conversation"]["last_response"]["ok"]));
    ASSERT_EQ(root["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(root["conversation"]["messages"][0]["role"].get<std::string>(), "user");
    EXPECT_EQ(root["conversation"]["messages"][1]["role"].get<std::string>(), "assistant");
    EXPECT_EQ(root["loop"]["status"].get<std::string>(), "turn-complete");
}

TEST(AgentRootVmOpsTest, ErrorResponseStillRecordsPromptWithoutAssistantMessage) {
    auto vm = makeVm(agentc::runtime::make_default_agent_root("system", "google", "gemini-2.5-flash"));
    const nlohmann::json response = {
        {"ok", false},
        {"message", nullptr},
        {"error", {{"code", "request_invalid"}, {"message", "broken"}}}
    };

    agentc::runtime::apply_runtime_response_to_vm_agent_root(*vm, "Bad prompt", response);

    const auto root = nlohmann::json::parse(agentc::toJson(vm->getCursor().getValue()));
    EXPECT_EQ(root["conversation"]["last_prompt"].get<std::string>(), "Bad prompt");
    EXPECT_EQ(root["conversation"]["assistant_text"].get<std::string>(), "");
    ASSERT_EQ(root["conversation"]["messages"].size(), 1u);
    EXPECT_EQ(root["conversation"]["messages"][0]["role"].get<std::string>(), "user");
    EXPECT_EQ(root["loop"]["status"].get<std::string>(), "turn-complete");
}

TEST(AgentRootVmOpsTest, RunTurnUsesVmOwnedHelpersAndRuntimeInvoker) {
    auto vm = makeVm(agentc::runtime::make_default_agent_root("system", "google", "gemini-2.5-flash"));
    nlohmann::json capturedRequest;

    const auto response = agentc::runtime::run_vm_agent_root_turn(
        *vm,
        "Say hello",
        [&](const nlohmann::json& request) {
            capturedRequest = request;
            return nlohmann::json{
                {"ok", true},
                {"message", {
                    {"role", "assistant"},
                    {"text", "Hello from full turn"}
                }}
            };
        });

    EXPECT_TRUE(jsonBoolish(response["ok"]));
    EXPECT_EQ(capturedRequest["system"].get<std::string>(), "system");
    EXPECT_EQ(capturedRequest["prompt"].get<std::string>(), "Say hello");
    EXPECT_EQ(agentc::runtime::reply_text_from_vm_agent_root(*vm), "Hello from full turn");

    const auto root = nlohmann::json::parse(agentc::toJson(vm->getCursor().getValue()));
    ASSERT_EQ(root["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(root["conversation"]["messages"][1]["text"].get<std::string>(), "Hello from full turn");
}

TEST(AgentRootVmOpsTest, ReplyTextFromVmRootFormatsStoredErrorEnvelope) {
    auto vm = makeVm(agentc::runtime::make_default_agent_root("system", "google", "gemini-2.5-flash"));
    const nlohmann::json response = {
        {"ok", false},
        {"message", nullptr},
        {"error", {{"code", "request_invalid"}, {"message", "broken"}}}
    };

    agentc::runtime::apply_runtime_response_to_vm_agent_root(*vm, "Bad prompt", response);
    EXPECT_EQ(agentc::runtime::reply_text_from_vm_agent_root(*vm), "[error] broken");
}

TEST(AgentRootVmOpsTest, RunTurnCanInvokeRuntimeThroughImportedVmBindings) {
    auto root = agentc::runtime::make_default_agent_root("system", "google", "gemini-2.5-flash");
    root["runtime"]["default_provider"] = "mock";
    root["runtime"]["default_model"] = "mock-model";
    auto vm = makeVm(root);

    const auto response = agentc::runtime::run_vm_agent_root_turn_via_imported_runtime(
        *vm,
        "Say hello through import",
        nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}},
        mockRuntimeArtifacts());

    EXPECT_TRUE(jsonBoolish(response["ok"]));
    EXPECT_EQ(response["message"]["text"].get<std::string>(), "mock:Say hello through import");
    EXPECT_EQ(agentc::runtime::reply_text_from_vm_agent_root(*vm), "mock:Say hello through import");

    const auto persisted = nlohmann::json::parse(agentc::toJson(vm->getCursor().getValue()));
    ASSERT_EQ(persisted["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(persisted["conversation"]["messages"][1]["text"].get<std::string>(), "mock:Say hello through import");
}
