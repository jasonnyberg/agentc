#pragma once

#include "../../../edict/edict_vm.h"

#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace agentc::runtime {

struct VmRuntimeImportArtifacts {
    std::string extensions_library_path;
    std::string extensions_header_path;
    std::string runtime_library_path;
    std::string runtime_header_path;
    std::string agentc_module_path;
    std::string agentc_stateful_loop_module_path;
    std::string agentc_provider_contracts_module_path;
    std::string agentc_agent_root_module_path;
};

VmRuntimeImportArtifacts discover_vm_runtime_import_artifacts();

nlohmann::json make_default_agent_root(const std::string& system_prompt,
                                       const std::string& default_provider,
                                       const std::string& default_model);

void run_vm_agent_turn_native(agentc::edict::EdictVM& vm, const std::string& prompt);

void rehydrate_vm_runtime_state(agentc::edict::EdictVM& vm,
                                const nlohmann::json& base_runtime_config,
                                const VmRuntimeImportArtifacts& artifacts,
                                const std::string& lifecycle_event,
                                const std::string& config_path_hint = std::string());

std::string reply_text_from_vm_agent_root(agentc::edict::EdictVM& vm);

} // namespace agentc::runtime
