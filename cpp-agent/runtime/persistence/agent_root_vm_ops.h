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
};

using VmAgentRuntimeInvoker = std::function<nlohmann::json(const nlohmann::json& request)>;

VmRuntimeImportArtifacts discover_vm_runtime_import_artifacts();

void install_vm_agent_root(agentc::edict::EdictVM& vm,
                           CPtr<agentc::ListreeValue> root_value,
                           const std::string& fallback_system_prompt,
                           const nlohmann::json& base_runtime_config,
                           const VmRuntimeImportArtifacts& artifacts,
                           const std::string& lifecycle_event,
                           const std::string& config_path_hint = std::string());

void reset_vm_agent_root(agentc::edict::EdictVM& vm,
                         const std::string& fallback_system_prompt,
                         const nlohmann::json& base_runtime_config,
                         const VmRuntimeImportArtifacts& artifacts,
                         const std::string& lifecycle_event,
                         const std::string& config_path_hint = std::string());

nlohmann::json build_request_from_vm_agent_root(agentc::edict::EdictVM& vm,
                                                const std::string& prompt);

void apply_runtime_response_to_vm_agent_root(agentc::edict::EdictVM& vm,
                                             const std::string& prompt,
                                             const nlohmann::json& response);

nlohmann::json make_default_agent_root(const std::string& system_prompt,
                                       const std::string& default_provider,
                                       const std::string& default_model);

nlohmann::json run_vm_agent_root_turn(agentc::edict::EdictVM& vm,
                                      const std::string& prompt,
                                      const VmAgentRuntimeInvoker& runtime_invoker);

nlohmann::json run_vm_agent_root_turn_via_imported_runtime(agentc::edict::EdictVM& vm,
                                                           const std::string& prompt,
                                                           const nlohmann::json& base_runtime_config,
                                                           const VmRuntimeImportArtifacts& artifacts);

void rehydrate_vm_runtime_state(agentc::edict::EdictVM& vm,
                                const std::string& fallback_system_prompt,
                                const nlohmann::json& base_runtime_config,
                                const VmRuntimeImportArtifacts& artifacts,
                                const std::string& lifecycle_event,
                                const std::string& config_path_hint = std::string());

std::string reply_text_from_vm_agent_root(agentc::edict::EdictVM& vm);

} // namespace agentc::runtime
