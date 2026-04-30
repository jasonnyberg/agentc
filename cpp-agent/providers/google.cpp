#include "google.h"
#include "../api_registry.h"
#include "../credentials.h"
#include "../http_client.h"
#include "../sse_parser.cpp" // Include parser directly for now

void register_google_provider() {
    register_provider("google-gemini-cli", [](
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts,
        AssistantMessageStream& stream)
    {
        HttpClient client;
        nlohmann::json payload;
        // Construct basic gemini content format
        nlohmann::json contents = nlohmann::json::array();
        for (const auto& msg : ctx.messages) {
            // Simplified conversion for demonstration
            if (const auto* u = std::get_if<UserMessage>(&msg)) {
                contents.push_back({{"role", "user"}, {"parts", {{"text", text_of(u->content)}}}});
            }
        }
        payload["contents"] = contents;

        HttpClient::Request req;
        req.url = model.base_url + "/v1beta/models/" + model.id + ":streamGenerateContent?key=" + opts.api_key.value_or("");
        req.body = payload.dump();
        req.headers = {"Content-Type: application/json"};

        SSEParser parser;
        parser.on_data = [&](const std::string& data) {
            if (data == "[DONE]") {
                AssistantMessage final_msg;
                final_msg.stop_reason = StopReason::stop;
                stream.push(EvDone{.reason = StopReason::stop, .message = final_msg});
                stream.end(final_msg);
            } else {
                try {
                    auto j = nlohmann::json::parse(data);
                    if (j.contains("candidates") && j["candidates"][0].contains("content")) {
                        std::string text = j["candidates"][0]["content"]["parts"][0]["text"];
                        AssistantMessage partial;
                        partial.content.push_back(TextContent{.text = text});
                        stream.push(EvTextDelta{.content_index = 0, .delta = text, .partial = partial});
                    }
                } catch (...) { /* ignore partial json */ }
            }
        };

        client.post_sse(req, [&](const char* buf, size_t len) {
            parser.feed(buf, len);
        });
    });
}
