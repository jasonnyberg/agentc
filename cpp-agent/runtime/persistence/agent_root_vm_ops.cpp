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

std::filesystem::path discover_provider_contracts_module_path() {
    const auto cwd = std::filesystem::current_path();
    const auto exePath = executable_path_or_empty();
    const auto exeDir = exePath.empty() ? std::filesystem::path() : exePath.parent_path();
    const auto buildDir = exeDir.filename() == "cpp-agent" ? exeDir.parent_path() : exeDir;
    return first_existing_path_or_throw(
        {cwd / "cpp-agent" / "edict" / "modules" / "agentc_provider_contracts.edict",
         buildDir.parent_path() / "cpp-agent" / "edict" / "modules" / "agentc_provider_contracts.edict"},
        "agentc_provider_contracts.edict module");
}

std::string provider_spec_word(const std::string& provider) {
    if (provider.empty() || provider == "google") return "google";
    if (provider == "local") return "local";
    if (provider == "openai") return "openai";
    if (provider == "openai-codex") return "openai_codex";
    return "unknown";
}

std::string extract_json_object_before_marker(const std::string& source,
                                              const std::string& marker) {
    const auto markerPos = source.find(marker);
    if (markerPos == std::string::npos) {
        throw std::runtime_error("provider catalog marker not found: " + marker);
    }
    const auto blockLineStart = source.rfind("\n[", markerPos);
    if (blockLineStart == std::string::npos) {
        throw std::runtime_error("provider catalog block start not found for: " + marker);
    }
    const auto blockStart = blockLineStart + 1;
    const auto objectStart = source.find('{', blockStart);
    if (objectStart == std::string::npos || objectStart > markerPos) {
        throw std::runtime_error("provider catalog JSON object not found for: " + marker);
    }

    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = objectStart; i < source.size(); ++i) {
        const char ch = source[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == '{') {
            ++depth;
        } else if (ch == '}') {
            --depth;
            if (depth == 0) {
                return source.substr(objectStart, i - objectStart + 1);
            }
        }
    }
    throw std::runtime_error("provider catalog JSON object is unterminated for: " + marker);
}

json provider_contract_json_from_edict(const std::string& provider) {
    const auto source = read_text_file_or_throw(discover_provider_contracts_module_path());
    const auto word = provider_spec_word(provider);
    const auto marker = std::string("] @agentc_provider_") + word + "_spec";
    auto spec = json::parse(extract_json_object_before_marker(source, marker));
    if (word == "unknown") {
        spec["id"] = provider;
        spec["runtime_provider"] = provider;
    }
    return spec;
}

json edict_runtime_config_json(const std::string& provider, const std::string& model) {
    auto spec = provider_contract_json_from_edict(provider);
    json runtimeConfig = {
        {"default_provider", spec.value("runtime_provider", provider)},
        {"default_model", model.empty() ? spec.value("default_model", std::string()) : model},
        {"provider_contract", spec}
    };
    return runtimeConfig;
}

CPtr<agentc::ListreeValue> edict_runtime_config_value(const std::string& provider,
                                                       const std::string& model) {
    return json_value_or_throw(edict_runtime_config_json(provider, model), "Edict provider runtime config");
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
           << shell_literal(artifacts.extensions_header_path) << " resolver.import! @ext\n";
    script << shell_literal(artifacts.runtime_library_path) << " "
           << shell_literal(artifacts.runtime_header_path) << " resolver.import! @runtimeffi\n";
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
    // Keep this cold-start skeleton allocation-neutral: callers may invoke it
    // after file-backed allocators are configured, and building it through a
    // temporary VM would allocate into the active session allocator.  The
    // semantic provider defaults and provider_contract are filled from Edict's
    // provider catalog by rehydrate_vm_runtime_state() on the actual session VM.
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
            {"provider_contract", nullptr}
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
        root agentc_agent_root_create_runtime! @runtime_handle
        runtime_handle root prompt agentc_agent_root_turn! @next_root
        runtime_handle agentc_destroy! /
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
            root->addItemValue(runtime_item, runtime, true);
        }
    }

    const auto runtime_json = json_from_value_or_throw(runtime, "runtime config");
    auto string_field = [](const json& object, const char* key, const std::string& fallback) -> std::string {
        return object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : fallback;
    };
    const std::string requested_provider = string_field(
        runtime_json, "default_provider", config_default_or(base_runtime_config, "default_provider", ""));
    const std::string requested_model = string_field(
        runtime_json, "default_model", config_default_or(base_runtime_config, "default_model", ""));

    // Fill missing runtime defaults and provider_contract from the Edict provider
    // catalog.  C++ may pass explicit config overrides, but it no longer mirrors
    // provider semantic defaults or request-contract fields itself.
    auto runtime_config_ltv = edict_runtime_config_value(requested_provider, requested_model);
    if (!runtime_config_ltv) {
        throw std::runtime_error("Edict provider runtime config construction returned null");
    }
    const auto resolved_runtime_json = json_from_value_or_throw(runtime_config_ltv, "Edict provider runtime config");
    auto set_runtime_string = [&](const char* key, const std::string& value) {
        auto item = runtime->find(key);
        auto val = agentc::createStringValue(value);
        if (item) {
            runtime->addItemValue(item, val, true);
        } else {
            agentc::addNamedItem(runtime, key, val);
        }
    };
    if (!runtime_json.contains("default_provider") || requested_provider.empty()) {
        set_runtime_string("default_provider", string_field(resolved_runtime_json, "default_provider", requested_provider));
    }
    if (!runtime_json.contains("default_model") || requested_model.empty()) {
        set_runtime_string("default_model", string_field(resolved_runtime_json, "default_model", requested_model));
    }

    auto provider_contract_item = runtime->find("provider_contract");
    auto provider_contract_ref = runtime_config_ltv->find("provider_contract");
    if (!provider_contract_ref || !provider_contract_ref->getValue(false, false)) {
        throw std::runtime_error("Edict provider runtime config is missing provider_contract");
    }
    auto provider_contract_ltv = provider_contract_ref->getValue(false, false);
    if (provider_contract_item) {
        runtime->addItemValue(provider_contract_item, provider_contract_ltv, true);
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
        runtime->addItemValue(rehydration_item, rehydration_ltv, true);
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
