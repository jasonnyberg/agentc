#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace agentc::runtime {

class Runtime {
public:
    Runtime();
    explicit Runtime(const std::string& config_json);

    void configure_json(const std::string& config_json);
    void configure_file(const std::string& config_path);

    nlohmann::json request_json(const std::string& request_json_text);

    std::string last_error_json() const;
    std::string last_trace_json() const;

private:
    nlohmann::json config_;
    nlohmann::json last_error_;
    nlohmann::json last_trace_;

    void clear_error();
    void set_error(const std::string& code,
                   const std::string& message,
                   bool retryable,
                   const nlohmann::json& provider_error = nullptr,
                   const nlohmann::json& context = nullptr);

    std::string resolve_provider(const nlohmann::json& request) const;
    std::string resolve_model(const std::string& provider, const nlohmann::json& request) const;
    std::string resolve_api(const std::string& provider) const;
    std::string resolve_base_url(const std::string& provider) const;
    std::string resolve_api_key(const std::string& provider, const nlohmann::json& request) const;

    nlohmann::json build_trace(const std::string& provider,
                               const std::string& model,
                               const nlohmann::json& request) const;
};

} // namespace agentc::runtime
