#include "openai_codex_provider.h"

#include "../../common/credentials.h"
#include "../../common/http_client.h"
#include "../../common/sse_parser.h"
#include "../../core/ai_types.h"
#include "../../core/provider_registry.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>

namespace agentc::runtime {
namespace {

using json = nlohmann::json;

std::string codex_url_for_base(const std::string& base_url) {
    std::string raw = base_url.empty() ? "https://chatgpt.com/backend-api" : base_url;
    while (!raw.empty() && raw.back() == '/') raw.pop_back();
    if (raw.size() >= std::string("/codex/responses").size() &&
        raw.compare(raw.size() - std::string("/codex/responses").size(), std::string::npos, "/codex/responses") == 0) {
        return raw;
    }
    if (raw.size() >= std::string("/codex").size() &&
        raw.compare(raw.size() - std::string("/codex").size(), std::string::npos, "/codex") == 0) {
        return raw + "/responses";
    }
    return raw + "/codex/responses";
}

json input_text(const std::string& text) {
    return json{{"type", "input_text"}, {"text", text}};
}

json output_text(const std::string& text) {
    return json{{"type", "output_text"}, {"text", text}, {"annotations", json::array()}};
}

json build_codex_input(const Context& ctx) {
    json input = json::array();
    for (const auto& msg : ctx.messages) {
        if (const auto* user = std::get_if<UserMessage>(&msg)) {
            const std::string text = text_of(user->content);
            if (!text.empty()) {
                input.push_back({{"role", "user"}, {"content", json::array({input_text(text)})}});
            }
            continue;
        }
        if (const auto* assistant = std::get_if<AssistantMessage>(&msg)) {
            const std::string text = text_of(assistant->content);
            if (!text.empty()) {
                input.push_back({
                    {"type", "message"},
                    {"role", "assistant"},
                    {"status", "completed"},
                    {"content", json::array({output_text(text)})}
                });
            }
            continue;
        }
        if (const auto* tool = std::get_if<ToolResultMessage>(&msg)) {
            const std::string text = text_of_tool(tool->content);
            input.push_back({
                {"type", "function_call_output"},
                {"call_id", tool->tool_call_id},
                {"output", text}
            });
        }
    }
    return input;
}

json build_request_body(const Model& model, const Context& ctx, const StreamOptions& opts) {
    json body;
    body["model"] = model.id;
    body["store"] = false;
    body["stream"] = true;
    body["instructions"] = ctx.system_prompt.value_or("You are a helpful assistant.");
    body["input"] = build_codex_input(ctx);
    body["text"] = json{{"verbosity", "low"}};
    body["include"] = json::array({"reasoning.encrypted_content"});
    body["tool_choice"] = "auto";
    body["parallel_tool_calls"] = true;
    body["reasoning"] = json{{"effort", opts.reasoning_effort.value_or("low")}, {"summary", "auto"}};
    if (opts.session_id) {
        body["prompt_cache_key"] = *opts.session_id;
    }
    if (opts.temperature) {
        body["temperature"] = *opts.temperature;
    }
    return body;
}

std::string error_message_from_event(const json& event) {
    if (event.contains("message") && event["message"].is_string()) {
        return event["message"].get<std::string>();
    }
    if (event.contains("response") && event["response"].is_object()) {
        const auto& response = event["response"];
        if (response.contains("error") && response["error"].is_object()) {
            const auto& error = response["error"];
            const std::string code = error.value("code", std::string("unknown"));
            const std::string message = error.value("message", std::string("no message"));
            return code + ": " + message;
        }
        if (response.contains("incomplete_details") && response["incomplete_details"].is_object()) {
            return "incomplete: " + response["incomplete_details"].value("reason", std::string("unknown"));
        }
    }
    return event.dump();
}

void apply_usage(const json& response, AssistantMessage& msg) {
    if (!response.contains("usage") || !response["usage"].is_object()) return;
    const auto& usage = response["usage"];
    int cached = 0;
    if (usage.contains("input_tokens_details") && usage["input_tokens_details"].is_object()) {
        cached = usage["input_tokens_details"].value("cached_tokens", 0);
    }
    const int input_total = usage.value("input_tokens", 0);
    msg.usage.input = input_total >= cached ? input_total - cached : input_total;
    msg.usage.output = usage.value("output_tokens", 0);
    msg.usage.cache_read = cached;
    msg.usage.cache_write = 0;
    msg.usage.total_tokens = usage.value("total_tokens", msg.usage.input + msg.usage.output + msg.usage.cache_read);
}

void apply_completed_response(const json& response, AssistantMessage& msg) {
    if (response.contains("id") && response["id"].is_string()) {
        msg.response_id = response["id"].get<std::string>();
    }
    apply_usage(response, msg);
    const std::string status = response.value("status", std::string("completed"));
    if (status == "incomplete") msg.stop_reason = StopReason::length;
    else if (status == "failed" || status == "cancelled") msg.stop_reason = StopReason::error;
    else msg.stop_reason = StopReason::stop;
}

std::string extract_done_item_text(const json& item) {
    if (!item.is_object() || item.value("type", std::string()) != "message") return {};
    if (!item.contains("content") || !item["content"].is_array()) return {};
    std::string text;
    for (const auto& part : item["content"]) {
        const std::string type = part.value("type", std::string());
        if (type == "output_text" && part.contains("text") && part["text"].is_string()) {
            text += part["text"].get<std::string>();
        } else if (type == "refusal" && part.contains("refusal") && part["refusal"].is_string()) {
            text += part["refusal"].get<std::string>();
        }
    }
    return text;
}

} // namespace

void register_openai_codex_provider() {
    agentc::runtime::register_provider("openai-codex-responses", [](
        const Model& model,
        const Context& ctx,
        const StreamOptions& opts,
        AssistantMessageStream& stream)
    {
        const auto creds = get_openai_codex_credentials();
        const std::string url = codex_url_for_base(model.base_url);
        const json body = build_request_body(model, ctx, opts);

        HttpClient::Request req;
        req.url = url;
        req.body = body.dump();
        req.headers.push_back("Authorization: Bearer " + creds.access);
        req.headers.push_back("chatgpt-account-id: " + creds.account_id);
        req.headers.push_back("originator: agentc");
        req.headers.push_back("OpenAI-Beta: responses=experimental");
        req.headers.push_back("Accept: text/event-stream");
        req.headers.push_back("Content-Type: application/json");
        if (opts.session_id) {
            req.headers.push_back("session_id: " + *opts.session_id);
            req.headers.push_back("x-client-request-id: " + *opts.session_id);
        }
        for (const auto& [k, v] : opts.extra_headers) {
            req.headers.push_back(k + ": " + v);
        }

        std::cerr << "[OpenAI Codex Provider] Outbound request:\n"
                  << "URL: " << req.url << "\n"
                  << "Model: " << model.id << "\n"
                  << "Body:\n" << req.body << std::endl;

        AssistantMessage final_msg;
        final_msg.api = model.api;
        final_msg.provider = model.provider;
        final_msg.model_id = model.id;
        final_msg.stop_reason = StopReason::stop;

        std::string full_text;
        bool emitted_start = false;
        bool completed = false;
        std::exception_ptr parse_error;

        auto ensure_text_block = [&]() {
            if (final_msg.content.empty()) {
                final_msg.content.push_back(TextContent{.text = ""});
            }
        };

        SSEParser parser;
        parser.on_data = [&](const std::string& data) {
            if (data == "[DONE]") return;
            try {
                const json event = json::parse(data);
                const std::string type = event.value("type", std::string());
                if (type == "error" || type == "response.failed") {
                    throw std::runtime_error(error_message_from_event(event));
                }
                if (type == "response.output_text.delta" && event.contains("delta") && event["delta"].is_string()) {
                    if (!emitted_start) {
                        emitted_start = true;
                        stream.push(EvStart{.partial = final_msg});
                        ensure_text_block();
                        stream.push(EvTextStart{.content_index = 0, .partial = final_msg});
                    }
                    const std::string delta = event["delta"].get<std::string>();
                    full_text += delta;
                    if (auto* text = std::get_if<TextContent>(&final_msg.content[0])) {
                        text->text = full_text;
                    }
                    stream.push(EvTextDelta{.content_index = 0, .delta = delta, .partial = final_msg});
                    return;
                }
                if (type == "response.output_item.done" && event.contains("item")) {
                    const std::string item_text = extract_done_item_text(event["item"]);
                    if (!item_text.empty() && full_text.empty()) {
                        full_text = item_text;
                        ensure_text_block();
                        if (auto* text = std::get_if<TextContent>(&final_msg.content[0])) {
                            text->text = full_text;
                        }
                    }
                    return;
                }
                if ((type == "response.completed" || type == "response.done" || type == "response.incomplete") && event.contains("response")) {
                    apply_completed_response(event["response"], final_msg);
                    completed = true;
                }
            } catch (...) {
                parse_error = std::current_exception();
            }
        };

        HttpClient client;
        const bool ok = client.post_sse(req, [&](const char* buf, size_t len) {
            parser.feed(buf, len);
        });

        if (parse_error) {
            stream.fail(parse_error);
            return;
        }
        if (!ok) {
            stream.fail(std::make_exception_ptr(std::runtime_error("OpenAI Codex request failed")));
            return;
        }
        if (!completed) {
            parser.finalize();
        }
        if (final_msg.content.empty() && !full_text.empty()) {
            final_msg.content.push_back(TextContent{.text = full_text});
        }
        if (!full_text.empty()) {
            if (!emitted_start) {
                stream.push(EvStart{.partial = final_msg});
            }
            stream.push(EvTextEnd{.content_index = 0, .text = full_text, .partial = final_msg});
        }
        stream.push(EvDone{.reason = final_msg.stop_reason, .message = final_msg});
        stream.end(final_msg);
    });
}

} // namespace agentc::runtime
