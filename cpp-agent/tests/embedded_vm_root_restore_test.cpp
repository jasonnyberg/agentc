#include <gtest/gtest.h>

#include "../runtime/persistence/agent_root_vm_ops.h"
#include "../runtime/persistence/session_state_store.h"
#include "../../edict/edict_vm.h"

#include <filesystem>
#include <nlohmann/json.hpp>

TEST(EmbeddedVmRootRestoreTest, ConstructsVmFromRestoredAnchoredRoot) {
    const auto base = (std::filesystem::temp_directory_path() / "agentc_vm_root_restore_test").string();
    agentc::runtime::SessionStateStore store(base);
    store.clear();

    auto rootJson = agentc::runtime::make_default_agent_root("persisted prompt", "google", "gemini-3.1-pro-preview");
    rootJson["conversation"]["messages"] = nlohmann::json::array({
        nlohmann::json{{"role", "user"}, {"text", "hello"}},
        nlohmann::json{{"role", "assistant"}, {"text", "world"}}
    });
    rootJson["loop"]["status"] = "turn-complete";

    auto rootValue = agentc::fromJson(rootJson.dump());
    ASSERT_TRUE(rootValue);

    std::string error;
    ASSERT_TRUE(store.saveRoot(rootValue, &error)) << error;

    CPtr<agentc::ListreeValue> restoredRoot;
    ASSERT_TRUE(store.loadRoot(restoredRoot, &error)) << error;
    ASSERT_TRUE(restoredRoot);

    agentc::edict::EdictVM vm(restoredRoot);
    const auto restoredJson = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()));

    ASSERT_TRUE(restoredJson["conversation"].is_object());
    EXPECT_EQ(restoredJson["conversation"]["system_prompt"].get<std::string>(), "persisted prompt");
    ASSERT_EQ(restoredJson["conversation"]["messages"].size(), 2u);
    EXPECT_EQ(restoredJson["conversation"]["messages"][1]["text"].get<std::string>(), "world");
    EXPECT_EQ(restoredJson["loop"]["status"].get<std::string>(), "turn-complete");

    store.clear();
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
        .agentc_agent_root_module_path = (sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc_agent_root.edict").string()
    };
}

TEST(EmbeddedVmRootRestoreTest, FullTurnPersistenceAndResume) {
    const auto base = (std::filesystem::temp_directory_path() / "agentc_vm_root_resume_test").string();
    agentc::runtime::SessionStateStore store(base);
    store.clear();

    const auto artifacts = mockRuntimeArtifacts();

    auto rootJson = agentc::runtime::make_default_agent_root("system", "mock", "mock-model");
    auto rootValue = agentc::fromJson(rootJson.dump());
    ASSERT_TRUE(rootValue);

    // Initial session run
    {
        agentc::edict::EdictVM vm(rootValue);
        printf("vm created\n"); fflush(stdout);
        agentc::runtime::rehydrate_vm_runtime_state(vm, nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}}, artifacts, "startup-fresh");
        
        std::cout << "Running first turn...\n" << std::flush;
        agentc::runtime::run_vm_agent_turn_native(vm, "first step");
        auto response = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()))["conversation"]["last_response"];
            
        std::cout << "Saving root...\n" << std::flush;
        std::string error;
        bool ok = store.saveRoot(vm.getCursor().getValue(), &error);
        printf("saveRoot returned %d\n", ok); fflush(stdout);
        ASSERT_TRUE(ok) << error;
        printf("ASSERT passed\n"); fflush(stdout);
    }

    std::cout << "Loading root...\n" << std::flush;
    // Restored session run
    {
        CPtr<agentc::ListreeValue> restoredRoot;
        std::string error;
        ASSERT_TRUE(store.loadRoot(restoredRoot, &error)) << error;
        ASSERT_TRUE(restoredRoot);

        std::cout << "Rehydrating...\n" << std::flush;
        agentc::edict::EdictVM vm(restoredRoot);
        agentc::runtime::rehydrate_vm_runtime_state(vm, nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}}, artifacts, "startup-restored");

        // Verify history was restored properly
        auto intermediateJson = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()));
        ASSERT_EQ(intermediateJson["conversation"]["messages"].size(), 2u);
        EXPECT_EQ(intermediateJson["conversation"]["messages"][1]["text"].get<std::string>(), "mock:first step");

        std::cout << "Running second turn...\n" << std::flush;
        // Issue another turn
        agentc::runtime::run_vm_agent_turn_native(vm, "second step");
        auto response = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()))["conversation"]["last_response"];

        // Verify state advanced correctly across restore
        auto finalJson = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()));
        ASSERT_EQ(finalJson["conversation"]["messages"].size(), 4u);
        EXPECT_EQ(finalJson["conversation"]["messages"][1]["text"].get<std::string>(), "mock:first step");
        EXPECT_EQ(finalJson["conversation"]["messages"][3]["text"].get<std::string>(), "mock:second step");
    }

    store.clear();
}

TEST(EmbeddedVmRootRestoreTest, CompareWarmToColdExecution) {
    const auto base_warm = (std::filesystem::temp_directory_path() / "agentc_vm_warm_test").string();
    const auto artifacts = mockRuntimeArtifacts();

    // Cold Run (Single uninterrupted session)
    nlohmann::json coldFinalState;
    {
        auto rootJson = agentc::runtime::make_default_agent_root("system", "mock", "mock-model");
        auto rootValue = agentc::fromJson(rootJson.dump());
        agentc::edict::EdictVM vm(rootValue);
        agentc::runtime::rehydrate_vm_runtime_state(vm, nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}}, artifacts, "startup-fresh");
        
        agentc::runtime::run_vm_agent_turn_native(vm, "step 1");
        agentc::runtime::run_vm_agent_turn_native(vm, "step 2");
        agentc::runtime::run_vm_agent_turn_native(vm, "step 3");
        
        coldFinalState = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()));
    }

    // Warm Run (Restarts between each step)
    nlohmann::json warmFinalState;
    agentc::runtime::SessionStateStore store(base_warm);
    store.clear();
    
    {
        auto rootJson = agentc::runtime::make_default_agent_root("system", "mock", "mock-model");
        auto rootValue = agentc::fromJson(rootJson.dump());
        agentc::edict::EdictVM vm(rootValue);
        agentc::runtime::rehydrate_vm_runtime_state(vm, nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}}, artifacts, "startup-fresh");
        
        agentc::runtime::run_vm_agent_turn_native(vm, "step 1");
        store.saveRoot(vm.getCursor().getValue());
    }
    
    {
        CPtr<agentc::ListreeValue> restoredRoot;
        store.loadRoot(restoredRoot);
        agentc::edict::EdictVM vm(restoredRoot);
        agentc::runtime::rehydrate_vm_runtime_state(vm, nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}}, artifacts, "startup-restored");
        
        agentc::runtime::run_vm_agent_turn_native(vm, "step 2");
        store.saveRoot(vm.getCursor().getValue());
    }

    {
        CPtr<agentc::ListreeValue> restoredRoot;
        store.loadRoot(restoredRoot);
        agentc::edict::EdictVM vm(restoredRoot);
        agentc::runtime::rehydrate_vm_runtime_state(vm, nlohmann::json{{"default_provider", "mock"}, {"default_model", "mock-model"}}, artifacts, "startup-restored");
        
        agentc::runtime::run_vm_agent_turn_native(vm, "step 3");
        
        warmFinalState = nlohmann::json::parse(agentc::toJson(vm.getCursor().getValue()));
    }

    coldFinalState.erase("__vm_runtime_handle");
    warmFinalState.erase("__vm_runtime_handle");
    if (coldFinalState.contains("runtime") && coldFinalState["runtime"].contains("rehydration")) {
        coldFinalState["runtime"]["rehydration"].erase("last_event");
    }
    if (warmFinalState.contains("runtime") && warmFinalState["runtime"].contains("rehydration")) {
        warmFinalState["runtime"]["rehydration"].erase("last_event");
    }

    auto erase_dynamic_addresses = [](nlohmann::json& node, auto& self) -> void {
        if (node.is_object()) {
            node.erase("resolver_address");
            for (auto& [key, val] : node.items()) {
                self(val, self);
            }
        } else if (node.is_array()) {
            for (auto& val : node) {
                self(val, self);
            }
        }
    };
    erase_dynamic_addresses(coldFinalState, erase_dynamic_addresses);
    erase_dynamic_addresses(warmFinalState, erase_dynamic_addresses);

    if (coldFinalState != warmFinalState) {
        std::ofstream("cold_debug.json") << coldFinalState.dump(2);
        std::ofstream("warm_debug.json") << warmFinalState.dump(2);
    }
    EXPECT_EQ(coldFinalState, warmFinalState);

    store.clear();
}
