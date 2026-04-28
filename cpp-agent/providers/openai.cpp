#include "openai.h"
#include "../api_registry.h"
#include "../credentials.h"
#include "../http_client.h"
#include "../sse_parser.cpp"

void register_openai_provider() {
    register_provider("openai-completions", [](
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
        // GitHub Copilot integration logic
        if (model.provider == "github-copilot") {
            auto creds = get_copilot_credentials();
            std::string access_token = creds.first;
            std::string base_url = creds.second;

            // Cast model and opts to non-const to update them
            Model& m = const_cast<Model&>(model);
            StreamOptions& o = const_cast<StreamOptions&>(opts);
            
            o.api_key = access_token;
            m.base_url = base_url;
            
            // Add dynamic Copilot headers
            req.headers.push_back("User-Agent: GitHubCopilotChat/0.35.0");
            req.headers.push_back("Editor-Version: vscode/1.107.0");
            req.headers.push_back("Editor-Plugin-Version: copilot-chat/0.35.0");
            req.headers.push_back("Copilot-Integration-Id: vscode-chat");
            req.headers.push_back("X-Initiator: user"); // Defaulting to user-initiated
            req.headers.push_back("Openai-Intent: conversation-edits");
        }
        
        req.headers.push_back("Authorization: Bearer " + opts.api_key.value_or(""));

        SSEParser parser;
        parser.on_data = [&](const std::string& data) {
            if (data == "[DONE]") {
                AssistantMessage final_msg;
                final_msg.stop_reason = StopReason::stop;
                stream.end(final_msg);
            } else {
                auto j = nlohmann::json::parse(data);
                if (j.contains("choices") && j["choices"][0]["delta"].contains("content")) {
                    std::string delta = j["choices"][0]["delta"]["content"];
                    AssistantMessage partial;
                    partial.content.push_back(TextContent{.text = delta});
                    stream.push(EvTextDelta{.content_index = 0, .delta = delta, .partial = partial});
                }
            }
        };

        client.post_sse(req, [&](const char* buf, size_t len) {
            parser.feed(buf, len);
        });
    });
}
