#include "openai_provider.h"

#include "../../../ai_types.h"
#include "../../core/provider_registry.h"
#include "../../common/credentials.h"
#include "../../common/http_client.h"
#include "../../common/sse_parser.h"

#include <stdexcept>

namespace agentc::runtime {

void register_openai_provider() {
    agentc::runtime::register_provider("openai-completions", [](
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts,
        AssistantMessageStream& stream)
    {
        HttpClient client;
        nlohmann::json payload;
        payload["model"] = model.id;
        payload["stream"] = true;

        nlohmann::json messages = nlohmann::json::array();
        if (ctx.system_prompt) {
            messages.push_back({{"role", "system"}, {"content", *ctx.system_prompt}});
        }
        for (const auto& msg : ctx.messages) {
            if (const auto* u = std::get_if<UserMessage>(&msg)) {
                messages.push_back({{"role", "user"}, {"content", text_of(u->content)}});
            } else if (const auto* a = std::get_if<AssistantMessage>(&msg)) {
                messages.push_back({{"role", "assistant"}, {"content", text_of(a->content)}});
            }
        }
        payload["messages"] = messages;

        HttpClient::Request req;
        req.url = model.base_url + "/chat/completions";
        req.body = payload.dump();

        std::string auth_token;
        if (model.provider == "github-copilot") {
            auto creds = get_copilot_credentials();
            auth_token = creds.first;
            req.url = creds.second + "/chat/completions";

            req.headers.push_back("User-Agent: GitHubCopilotChat/0.35.0");
            req.headers.push_back("Editor-Version: vscode/1.107.0");
            req.headers.push_back("Editor-Plugin-Version: copilot-chat/0.35.0");
            req.headers.push_back("Copilot-Integration-Id: vscode-chat");
            req.headers.push_back("X-Initiator: user");
            req.headers.push_back("Openai-Intent: conversation-edits");
        } else {
            auth_token = opts.api_key.value_or(get_openai_api_key());
        }

        req.headers.push_back("Authorization: Bearer " + auth_token);
        req.headers.push_back("Content-Type: application/json");

        AssistantMessage final_msg;
        final_msg.api = model.api;
        final_msg.provider = model.provider;
        final_msg.model_id = model.id;
        final_msg.stop_reason = StopReason::stop;

        std::string full_text;
        bool completed = false;

        SSEParser parser;
        parser.on_data = [&](const std::string& data) {
            if (data == "[DONE]") {
                if (!full_text.empty() && final_msg.content.empty()) {
                    final_msg.content.push_back(TextContent{.text = full_text});
                }
                completed = true;
                stream.push(EvDone{.reason = final_msg.stop_reason, .message = final_msg});
                stream.end(final_msg);
                return;
            }

            try {
                auto j = nlohmann::json::parse(data);
                if (j.contains("id") && j["id"].is_string()) {
                    final_msg.response_id = j["id"].get<std::string>();
                }
                if (j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()) {
                    const auto& choice = j["choices"][0];
                    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
                        const auto finish = choice["finish_reason"].get<std::string>();
                        if (finish == "tool_calls") {
                            final_msg.stop_reason = StopReason::tool_use;
                        } else if (finish == "length") {
                            final_msg.stop_reason = StopReason::length;
                        } else if (finish == "stop") {
                            final_msg.stop_reason = StopReason::stop;
                        }
                    }
                    if (choice.contains("delta") && choice["delta"].contains("content")) {
                        std::string delta = choice["delta"]["content"].get<std::string>();
                        full_text += delta;
                        AssistantMessage partial = final_msg;
                        partial.content.clear();
                        partial.content.push_back(TextContent{.text = full_text});
                        stream.push(EvTextDelta{.content_index = 0, .delta = delta, .partial = partial});
                    }
                }
            } catch (...) {
            }
        };

        const bool ok = client.post_sse(req, [&](const char* buf, size_t len) {
            parser.feed(buf, len);
        });

        if (!ok) {
            stream.fail(std::make_exception_ptr(std::runtime_error("OpenAI-compatible request failed")));
            return;
        }

        if (!completed) {
            parser.finalize();
        }
    });
}

} // namespace agentc::runtime
