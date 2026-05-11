#include "google_provider.h"

#include "../../core/ai_types.h"
#include "../../core/provider_registry.h"
#include "../../common/credentials.h"
#include "../../common/http_client.h"
#include "../../common/sse_parser.h"

#include <stdexcept>

namespace agentc::runtime {

void register_google_provider() {
    agentc::runtime::register_provider("google-gemini-cli", [](
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
        req.url = model.base_url + "/v1beta/models/" + model.id + ":streamGenerateContent?alt=sse&key=" + api_key;
        req.body = payload.dump();
        req.headers = {"Content-Type: application/json"};

        AssistantMessage final_msg;
        final_msg.api = model.api;
        final_msg.provider = model.provider;
        final_msg.model_id = model.id;
        final_msg.stop_reason = StopReason::stop;

        std::string full_text;
        bool completed = false;
        bool failed = false;

        const auto finish = [&]() {
            if (completed || failed) {
                return;
            }
            if (final_msg.content.empty()) {
                final_msg.content.push_back(TextContent{.text = full_text});
            }
            completed = true;
            stream.push(EvDone{.reason = final_msg.stop_reason, .message = final_msg});
            stream.end(final_msg);
        };

        SSEParser parser;
        parser.on_data = [&](const std::string& data) {
            if (data == "[DONE]") {
                finish();
                return;
            }

            try {
                auto j = nlohmann::json::parse(data);
                if (j.contains("error")) {
                    failed = true;
                    const auto msg = j["error"].value("message", std::string("Unknown Google API error"));
                    stream.fail(std::make_exception_ptr(std::runtime_error(msg)));
                    return;
                }
                if (j.contains("responseId") && j["responseId"].is_string()) {
                    final_msg.response_id = j["responseId"].get<std::string>();
                }
                if (!j.contains("candidates") || j["candidates"].empty()) {
                    return;
                }

                const auto& candidate = j["candidates"][0];
                if (candidate.contains("finishReason") && candidate["finishReason"].is_string()) {
                    const auto reason = candidate["finishReason"].get<std::string>();
                    if (reason == "MAX_TOKENS") {
                        final_msg.stop_reason = StopReason::length;
                    } else if (reason == "STOP") {
                        final_msg.stop_reason = StopReason::stop;
                    }
                }

                if (candidate.contains("content") && candidate["content"].contains("parts")) {
                    for (const auto& part : candidate["content"]["parts"]) {
                        if (part.value("thought", false) || !part.contains("text") || !part["text"].is_string()) {
                            continue;
                        }
                        const std::string delta = part["text"].get<std::string>();
                        if (delta.empty()) {
                            continue;
                        }
                        full_text += delta;
                        AssistantMessage partial = final_msg;
                        partial.content.clear();
                        partial.content.push_back(TextContent{.text = full_text});
                        stream.push(EvTextDelta{.content_index = 0, .delta = delta, .partial = partial});
                    }
                }

                if (candidate.contains("finishReason") && candidate["finishReason"].is_string()) {
                    finish();
                }
            } catch (const std::exception& e) {
                failed = true;
                stream.fail(std::make_exception_ptr(std::runtime_error(std::string("Google stream JSON parse failure: ") + e.what())));
            }
        };

        bool ok = client.post_sse(req, [&](const char* buf, size_t len) {
            parser.feed(buf, len);
        });

        if (!ok) {
            stream.fail(std::make_exception_ptr(std::runtime_error("Google streaming request failed")));
            return;
        }

        if (!completed && !failed) {
            parser.finalize();
        }
    });
}

} // namespace agentc::runtime
