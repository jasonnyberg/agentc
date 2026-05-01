#include "google_provider.h"

#include "../../../ai_types.h"
#include "../../../api_registry.h"
#include "../../common/credentials.h"
#include "../../common/http_client.h"

#include <cstdio>
#include <stdexcept>

namespace agentc::runtime {

void register_google_provider() {
    ::register_provider("google-gemini-cli", [](
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts,
        AssistantMessageStream& stream)
    {
        HttpClient client;
        nlohmann::json payload;
        nlohmann::json contents = nlohmann::json::array();

        for (const auto& msg : ctx.messages) {
            if (const auto* u = std::get_if<UserMessage>(&msg)) {
                contents.push_back({{"role", "user"}, {"parts", {{{"text", text_of(u->content)}}}}});
            } else if (const auto* a = std::get_if<AssistantMessage>(&msg)) {
                contents.push_back({{"role", "model"}, {"parts", {{{"text", text_of(a->content)}}}}});
            }
        }

        payload["contents"] = contents;
        if (ctx.system_prompt && !ctx.system_prompt->empty()) {
            payload["systemInstruction"] = {{"parts", {{{"text", *ctx.system_prompt}}}}};
        }

        const std::string api_key = opts.api_key.value_or(get_google_api_key());

        HttpClient::Request req;
        req.url = model.base_url + "/v1beta/models/" + model.id + ":generateContent?key=" + api_key;
        req.body = payload.dump();
        req.headers = {"Content-Type: application/json"};

        std::string response_buffer;
        bool ok = client.post_sse(req, [&](const char* buf, size_t len) {
            response_buffer.append(buf, len);
        });

        std::printf("[google] request url: %s\n", req.url.c_str());
        std::printf("[google] raw response: %s\n", response_buffer.c_str());

        if (!ok) {
            stream.fail(std::make_exception_ptr(std::runtime_error("Google request failed")));
            return;
        }

        try {
            auto j = nlohmann::json::parse(response_buffer);

            if (j.contains("error")) {
                const auto msg = j["error"].value("message", std::string("Unknown Google API error"));
                stream.fail(std::make_exception_ptr(std::runtime_error(msg)));
                return;
            }

            if (!j.contains("candidates") || j["candidates"].empty()) {
                stream.fail(std::make_exception_ptr(std::runtime_error("Google response missing candidates")));
                return;
            }

            auto& candidate = j["candidates"][0];
            AssistantMessage final_msg;
            final_msg.api = model.api;
            final_msg.provider = model.provider;
            final_msg.model_id = model.id;
            final_msg.stop_reason = StopReason::stop;
            if (j.contains("responseId")) {
                final_msg.response_id = j["responseId"].get<std::string>();
            }

            std::string text;
            if (candidate.contains("content") && candidate["content"].contains("parts")) {
                for (const auto& part : candidate["content"]["parts"]) {
                    if (part.contains("text")) {
                        text += part["text"].get<std::string>();
                    }
                }
            }

            final_msg.content.push_back(TextContent{.text = text});

            AssistantMessage partial;
            partial.api = final_msg.api;
            partial.provider = final_msg.provider;
            partial.model_id = final_msg.model_id;
            partial.content.push_back(TextContent{.text = text});
            stream.push(EvTextDelta{.content_index = 0, .delta = text, .partial = partial});
            stream.push(EvDone{.reason = final_msg.stop_reason, .message = final_msg});
            stream.end(final_msg);
        } catch (const std::exception& e) {
            stream.fail(std::make_exception_ptr(std::runtime_error(std::string("Google JSON parse failure: ") + e.what())));
        }
    });
}

} // namespace agentc::runtime
