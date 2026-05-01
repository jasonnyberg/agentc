#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace agentc::runtime {

nlohmann::json make_default_agent_root(const std::string& system_prompt,
                                      const std::string& default_provider,
                                      const std::string& default_model);

nlohmann::json normalize_agent_root(const nlohmann::json& value,
                                    const std::string& fallback_system_prompt,
                                    const std::string& fallback_provider,
                                    const std::string& fallback_model);

nlohmann::json build_request_from_agent_root(const nlohmann::json& root,
                                             const std::string& prompt);

void apply_runtime_response_to_agent_root(nlohmann::json& root,
                                          const std::string& prompt,
                                          const nlohmann::json& response);

} // namespace agentc::runtime
