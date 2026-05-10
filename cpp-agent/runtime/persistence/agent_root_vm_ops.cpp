#include "agent_root_vm_ops.h"

#include "../../../edict/edict_compiler.h"
#include "../../../edict/edict_repl.h"
#include "../../../listree/listree.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace agentc::runtime {
namespace {

using json = nlohmann::json;

json provider_contract_json_for(const std::string& provider) {
    if (provider == "local") {
        return json{{"id", "local"},
                    {"family", "openai-compatible"},
                    {"runtime_provider", "local"},
                    {"default_model", "qwen"},
                    {"request_contract", "canonical-chat-v1"},
                    {"transport_api", "openai-completions"},
                    {"message_field", "content"},
                    {"response_text_path", "message.text"},
                    {"capabilities", json{{"streaming", "true"}, {"system_prompt", "true"}, {"history", "true"}}}};
    }
    if (provider == "openai") {
        return json{{"id", "openai"},
                    {"family", "openai-compatible"},
                    {"runtime_provider", "openai"},
                    {"default_model", "gpt-4.1"},
                    {"request_contract", "canonical-chat-v1"},
                    {"transport_api", "openai-completions"},
                    {"message_field", "content"},
                    {"response_text_path", "message.text"},
                    {"capabilities", json{{"streaming", "true"}, {"system_prompt", "true"}, {"history", "true"}}}};
    }
    if (provider == "google") {
        return json{{"id", "google"},
                    {"family", "gemini-generate-content"},
                    {"runtime_provider", "google"},
                    {"default_model", "gemini-3.1-pro-preview"},
                    {"request_contract", "canonical-chat-v1"},
                    {"transport_api", "google-gemini-cli"},
                    {"message_field", "parts[].text"},
                    {"response_text_path", "message.text"},
                    {"capabilities", json{{"streaming", "true"}, {"system_prompt", "true"}, {"history", "true"}}}};
    }
    return json{{"id", provider},
                {"family", "custom"},
                {"runtime_provider", provider},
                {"default_model", ""},
                {"request_contract", "canonical-chat-v1"},
                {"transport_api", "runtime-native"},
                {"message_field", "content"},
                {"response_text_path", "message.text"},
                {"capabilities", json{{"streaming", "true"}, {"system_prompt", "true"}, {"history", "true"}}}};
}


CPtr<agentc::ListreeValue> json_value_or_throw(const json& value, const char* label) {
    auto out = agentc::fromJson(value.dump());
    if (!out) {
        throw std::runtime_error(std::string("failed to materialize ") + label + " into Listree");
    }
    return out;
}

json json_from_value_or_throw(CPtr<agentc::ListreeValue> value, const char* label) {
    if (!value) {
        throw std::runtime_error(std::string("missing ") + label + " value");
    }
    return json::parse(agentc::toJson(value));
}

bool json_boolish(const json& value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_string()) {
        return value.get<std::string>() == "true";
    }
    return false;
}

std::string read_text_file_or_throw(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.good()) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path executable_path_or_empty() {
    std::error_code ec;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : path;
}

std::filesystem::path first_existing_path_or_throw(std::initializer_list<std::filesystem::path> candidates,
                                                   const char* label) {
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error(std::string("failed to locate ") + label);
}

std::string shell_literal(const std::string& value) {
    return "[" + value + "]";
}


json runtime_rehydration_metadata(const json& /*base_runtime_config*/,
                                  const VmRuntimeImportArtifacts& artifacts,
                                  const std::string& lifecycle_event,
                                  const std::string& config_path_hint) {
    json metadata = {
        {"status", "complete"},
        {"last_event", lifecycle_event},
        {"config_source", config_path_hint.empty() ? "host-defaults" : "config-file"},
        {"transient_handles_persisted", "not-persisted"},
        {"transient_state_rebuilt", "rebuilt"},
        {"binding", {
            {"kind", "embedded_imported_agentc_runtime"},
            {"module_name", "agentc"},
            {"runtime_library", std::filesystem::path(artifacts.runtime_library_path).filename().string()},
            {"extensions_library", std::filesystem::path(artifacts.extensions_library_path).filename().string()}
        }}
    };
    if (!config_path_hint.empty()) {
        metadata["config_path_hint"] = config_path_hint;
    }
    return metadata;
}

std::string runtime_bootstrap_script_for_artifacts(const VmRuntimeImportArtifacts& artifacts) {
    std::ostringstream script;
    script << shell_literal(artifacts.extensions_library_path) << " "
           << shell_literal(artifacts.extensions_header_path) << " resolver.import ! @ext\n";
    script << shell_literal(artifacts.runtime_library_path) << " "
           << shell_literal(artifacts.runtime_header_path) << " resolver.import ! @runtimeffi\n";
    script << read_text_file_or_throw(artifacts.agentc_module_path) << "\n";
    script << read_text_file_or_throw(artifacts.agentc_stateful_loop_module_path) << "\n";
    script << read_text_file_or_throw(artifacts.agentc_provider_contracts_module_path) << "\n";
    script << read_text_file_or_throw(artifacts.llm_module_path) << "\n";
    script << read_text_file_or_throw(artifacts.agentc_agent_root_module_path) << "\n";
    return script.str();
}



std::string config_default_or(const json& config, const char* key, const char* fallback) {
    return config.contains(key) && config[key].is_string()
        ? config[key].get<std::string>()
        : std::string(fallback);
}

} // namespace

nlohmann::json make_default_agent_root(const std::string& system_prompt,
                                       const std::string& default_provider,
                                       const std::string& default_model) {
    return json{
        {"conversation", {
            {"system_prompt", system_prompt},
            {"messages", json::array()},
            {"last_prompt", nullptr},
            {"last_response", nullptr},
            {"assistant_text", ""}
        }},
        {"memory", {
            {"entries", json::array()}
        }},
        {"policy", {
            {"allow_tools", "false"}
        }},
        {"runtime", {
            {"default_provider", default_provider},
            {"default_model", default_model},
            {"provider_contract", provider_contract_json_for(default_provider)}
        }},
        {"loop", {
            {"status", "ready"},
            {"last_prompt", nullptr}
        }}
    };
}



VmRuntimeImportArtifacts discover_vm_runtime_import_artifacts() {
    const auto cwd = std::filesystem::current_path();
    const auto exePath = executable_path_or_empty();
    const auto exeDir = exePath.empty() ? std::filesystem::path() : exePath.parent_path();
    const auto buildDir = exeDir.filename() == "cpp-agent" ? exeDir.parent_path() : exeDir;
    const auto sourceRoot = first_existing_path_or_throw(
        {cwd / "cpp-agent" / "edict" / "modules" / "agentc.edict",
         buildDir.parent_path() / "cpp-agent" / "edict" / "modules" / "agentc.edict"},
        "agentc.edict source root").parent_path().parent_path().parent_path().parent_path();

    VmRuntimeImportArtifacts artifacts;
    artifacts.extensions_library_path = first_existing_path_or_throw(
        {buildDir / "extensions" / "libagentc_extensions.so", cwd / "build" / "extensions" / "libagentc_extensions.so"},
        "agentc extensions library").string();
    artifacts.extensions_header_path = first_existing_path_or_throw(
        {sourceRoot / "extensions" / "agentc_stdlib.h"},
        "agentc extensions header").string();
    artifacts.runtime_library_path = first_existing_path_or_throw(
        {exeDir / "libagent_runtime.so", buildDir / "cpp-agent" / "libagent_runtime.so", cwd / "build" / "cpp-agent" / "libagent_runtime.so"},
        "agent runtime library").string();
    artifacts.runtime_header_path = first_existing_path_or_throw(
        {sourceRoot / "cpp-agent" / "include" / "agentc_runtime" / "agentc_runtime.h"},
        "agent runtime header").string();
    artifacts.agentc_module_path = first_existing_path_or_throw(
        {sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc.edict"},
        "agentc.edict module").string();
    artifacts.agentc_stateful_loop_module_path = first_existing_path_or_throw(
        {sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc_stateful_loop.edict"},
        "agentc_stateful_loop.edict module").string();
    artifacts.agentc_provider_contracts_module_path = first_existing_path_or_throw(
        {sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc_provider_contracts.edict"},
        "agentc_provider_contracts.edict module").string();
    artifacts.llm_module_path = first_existing_path_or_throw(
        {sourceRoot / "cpp-agent" / "edict" / "modules" / "llm.edict"},
        "llm.edict module").string();
    artifacts.agentc_agent_root_module_path = first_existing_path_or_throw(
        {sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc_agent_root.edict"},
        "agentc_agent_root.edict module").string();
    return artifacts;
}

void run_vm_agent_turn_native(agentc::edict::EdictVM& vm, const std::string& prompt) {
    const std::string script = R"(
        @prompt
        @root
        root agentc_agent_root_create_runtime ! @runtime_handle
        runtime_handle root prompt agentc_agent_root_turn ! @next_root
        runtime_handle agentc_destroy ! /
        next_root
    )";

    agentc::edict::EdictCompiler compiler;
    auto code = compiler.compile(script);
    if (code.getData().empty()) {
        throw std::runtime_error("Failed to compile native turn script");
    }

    vm.pushData(vm.getCursor().getValue());
    vm.pushData(agentc::createStringValue(prompt));

    if (vm.execute(code) & agentc::edict::VM_ERROR) {
        const std::string error = vm.getError();
        vm.reset();
        throw std::runtime_error("Native Edict turn failed: " + error);
    }

    auto next_root = vm.popData();
    if (next_root) {
        vm.setCursor(next_root);
    }
    vm.reset();
}

void rehydrate_vm_runtime_state(agentc::edict::EdictVM& vm,
                                const json& base_runtime_config,
                                const VmRuntimeImportArtifacts& artifacts,
                                const std::string& lifecycle_event,
                                const std::string& config_path_hint) {
    auto root = vm.getCursor().getValue();
    if (!root) {
        return; // Empty or invalid root, let normal startup path handle initialization
    }

    // Re-import missing bindings (Durable Binding Import)
    if (!root->find("agentc_call", false) || !root->find("ext", false) || !root->find("runtimeffi", false)) {
        std::ostringstream script;
        script << runtime_bootstrap_script_for_artifacts(artifacts);
        agentc::edict::EdictCompiler compiler;
        auto code = compiler.compile(script.str());
        if (code.getData().empty()) {
            throw std::runtime_error("Failed to compile runtime bootstrap script in rehydrate_vm_runtime_state");
        }
        if (vm.execute(code) & agentc::edict::VM_ERROR) {
            const std::string error = vm.getError();
            vm.reset();
            throw std::runtime_error("Failed to execute runtime bootstrap script in rehydrate_vm_runtime_state: " + error);
        }
    }

    // Remove transient runtime scratch state
    root->remove("__vm_runtime_response");
    root->remove("vm_runtime_handle");

    // Find or create "runtime" block
    auto runtime_item = root->find("runtime");
    CPtr<agentc::ListreeValue> runtime;
    if (!runtime_item) {
        runtime = agentc::createNullValue(); // It will become an object when we add named items
        agentc::addNamedItem(root, "runtime", runtime);
    } else {
        runtime = runtime_item->getValue(false, false);
        if (!runtime) {
            runtime = agentc::createNullValue();
            runtime_item->addValue(runtime, true);
        }
    }

    // Ensure default_provider exists
    auto provider_item = runtime->find("default_provider");
    if (!provider_item || !provider_item->getValue(false, false) || provider_item->getValue(false, false)->isListMode()) {
        auto provider_val = agentc::createStringValue(config_default_or(base_runtime_config, "default_provider", "google"));
        if (provider_item) {
            provider_item->addValue(provider_val, true);
        } else {
            agentc::addNamedItem(runtime, "default_provider", provider_val);
        }
    }

    // Ensure default_model exists
    auto model_item = runtime->find("default_model");
    if (!model_item || !model_item->getValue(false, false) || model_item->getValue(false, false)->isListMode()) {
        auto model_val = agentc::createStringValue(config_default_or(base_runtime_config, "default_model", "gemini-3.1-pro-preview"));
        if (model_item) {
            model_item->addValue(model_val, true);
        } else {
            agentc::addNamedItem(runtime, "default_model", model_val);
        }
    }

    const std::string provider_name = config_default_or(base_runtime_config, "default_provider", "google");
    auto provider_contract_item = runtime->find("provider_contract");
    auto provider_contract_ltv = json_value_or_throw(provider_contract_json_for(provider_name), "provider contract");
    if (provider_contract_item) {
        provider_contract_item->addValue(provider_contract_ltv, true);
    } else {
        agentc::addNamedItem(runtime, "provider_contract", provider_contract_ltv);
    }

    // Build the rehydration metadata JSON block and inject it
    auto rehydration_json = runtime_rehydration_metadata(
        base_runtime_config,
        artifacts,
        lifecycle_event,
        config_path_hint);
    
    auto rehydration_ltv = json_value_or_throw(rehydration_json, "rehydration metadata");
    
    auto rehydration_item = runtime->find("rehydration");
    if (rehydration_item) {
        rehydration_item->addValue(rehydration_ltv, true);
    } else {
        agentc::addNamedItem(runtime, "rehydration", rehydration_ltv);
    }
}

std::string reply_text_from_vm_agent_root(agentc::edict::EdictVM& vm) {
    const auto root = json_from_value_or_throw(vm.getCursor().getValue(), "embedded vm root");
    if (!root.contains("conversation") || !root["conversation"].is_object()) {
        return "[no assistant text returned]";
    }

    const auto& conversation = root["conversation"];
    if (conversation.contains("assistant_text") && conversation["assistant_text"].is_string()) {
        const auto text = conversation["assistant_text"].get<std::string>();
        if (!text.empty()) {
            return text;
        }
    }

    if (conversation.contains("last_response") && conversation["last_response"].is_object()) {
        const auto& response = conversation["last_response"];
        if (response.contains("ok") && !json_boolish(response["ok"])) {
            if (response.contains("error") && response["error"].is_object()) {
                return std::string("[error] ") + response["error"].value("message", std::string("unknown runtime error"));
            }
            return "[error] request failed";
        }
    }

    return "[no assistant text returned]";
}

} // namespace agentc::runtime
