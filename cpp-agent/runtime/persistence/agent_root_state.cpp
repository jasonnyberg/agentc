#include "agent_root_state.h"

namespace agentc::runtime {
namespace {

using json = nlohmann::json;

void merge_object(json& dst, const json& src) {
    if (!dst.is_object() || !src.is_object()) {
        return;
    }
    for (auto it = src.begin(); it != src.end(); ++it) {
        dst[it.key()] = it.value();
    }
}

json normalized_conversation(const json& root,
                             const std::string& fallback_system_prompt) {
    json conversation = {
        {"system_prompt", fallback_system_prompt},
        {"messages", json::array()},
        {"last_prompt", nullptr},
        {"last_response", nullptr},
        {"assistant_text", ""}
    };

    if (root.is_object() && root.contains("conversation") && root["conversation"].is_object()) {
        merge_object(conversation, root["conversation"]);
    } else if (root.is_object()) {
        if (root.contains("system_prompt") && root["system_prompt"].is_string()) {
            conversation["system_prompt"] = root["system_prompt"];
        }
        if (root.contains("messages") && root["messages"].is_array()) {
            conversation["messages"] = root["messages"];
        }
    }

    if (!conversation.contains("messages") || !conversation["messages"].is_array()) {
        conversation["messages"] = json::array();
    }
    if (!conversation.contains("assistant_text") || !conversation["assistant_text"].is_string()) {
        conversation["assistant_text"] = "";
    }
    if (!conversation.contains("system_prompt") || !conversation["system_prompt"].is_string()) {
        conversation["system_prompt"] = fallback_system_prompt;
    }

    return conversation;
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
            {"default_model", default_model}
        }},
        {"loop", {
            {"status", "ready"},
            {"last_prompt", nullptr}
        }}
    };
}

nlohmann::json normalize_agent_root(const nlohmann::json& value,
                                    const std::string& fallback_system_prompt,
                                    const std::string& fallback_provider,
                                    const std::string& fallback_model) {
    json root = make_default_agent_root(fallback_system_prompt, fallback_provider, fallback_model);

    if (!value.is_object()) {
        return root;
    }

    root["conversation"] = normalized_conversation(value, fallback_system_prompt);

    if (value.contains("memory") && value["memory"].is_object()) {
        merge_object(root["memory"], value["memory"]);
    }
    if (value.contains("policy") && value["policy"].is_object()) {
        merge_object(root["policy"], value["policy"]);
    }
    if (value.contains("runtime") && value["runtime"].is_object()) {
        merge_object(root["runtime"], value["runtime"]);
    }
    if (value.contains("loop") && value["loop"].is_object()) {
        merge_object(root["loop"], value["loop"]);
    }

    return root;
}

nlohmann::json build_request_from_agent_root(const nlohmann::json& root,
                                             const std::string& prompt) {
    const json normalized = normalize_agent_root(root, "", "google", "gemini-2.5-flash");
    return json{
        {"system", normalized["conversation"]["system_prompt"]},
        {"messages", normalized["conversation"]["messages"]},
        {"prompt", prompt},
        {"response_mode", "text"}
    };
}

void apply_runtime_response_to_agent_root(nlohmann::json& root,
                                          const std::string& prompt,
                                          const nlohmann::json& response) {
    root = normalize_agent_root(root, "", "google", "gemini-2.5-flash");

    auto& conversation = root["conversation"];
    if (!conversation["messages"].is_array()) {
        conversation["messages"] = json::array();
    }

    conversation["messages"].push_back(json{{"role", "user"}, {"text", prompt}});
    conversation["last_prompt"] = prompt;
    conversation["last_response"] = response;

    std::string assistant_text;
    if (response.is_object() && response.contains("message") && response["message"].is_object()) {
        const auto& message = response["message"];
        conversation["messages"].push_back(message);
        if (message.contains("text") && message["text"].is_string()) {
            assistant_text = message["text"].get<std::string>();
        }
    }
    conversation["assistant_text"] = assistant_text;

    root["loop"]["last_prompt"] = prompt;
    root["loop"]["status"] = "turn-complete";
}

} // namespace agentc::runtime
