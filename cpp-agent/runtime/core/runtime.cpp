#include "runtime.h"

#include "provider_registry.h"
#include "../providers/register_builtin_providers.h"
#include "../common/credentials.h"
#include "ai_types.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace agentc::runtime {
namespace {

using json = nlohmann::json;

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) {
        throw std::runtime_error("Failed to open config file: " + path);
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string default_base_url_for_provider(const std::string& provider) {
    if (provider == "google") {
        return "https://generativelanguage.googleapis.com";
    }
    if (provider == "openai") {
        return "https://api.openai.com";
    }
    if (provider == "local") {
        return "http://localhost:8081/v1";
    }
    if (provider == "github-copilot") {
        return "https://api.individual.githubcopilot.com";
    }
    if (provider == "openai-codex") {
        return "https://chatgpt.com/backend-api";
    }
    return {};
}

std::string generated_request_id() {
    return "req-" + std::to_string(UserMessage::now_ms());
}

Context build_context_from_request(const json& request) {
    Context ctx;

    if (request.contains("system") && request["system"].is_string()) {
        ctx.system_prompt = request["system"].get<std::string>();
    }

    if (request.contains("messages") && request["messages"].is_array()) {
        for (const auto& msg : request["messages"]) {
            const std::string role = msg.value("role", "");
            std::string text;
            if (msg.contains("text") && msg["text"].is_string()) {
                text = msg["text"].get<std::string>();
            } else if (msg.contains("content") && msg["content"].is_string()) {
                text = msg["content"].get<std::string>();
            }
            if (role == "system") {
                if (!ctx.system_prompt && !text.empty()) {
                    ctx.system_prompt = text;
                }
                continue;
            }
            if (role == "user") {
                ctx.messages.push_back(UserMessage::text(text));
                continue;
            }
            if (role == "assistant") {
                AssistantMessage assistant;
                assistant.timestamp = UserMessage::now_ms();
                if (!text.empty()) {
                    assistant.content.push_back(TextContent{.text = text});
                }
                ctx.messages.push_back(assistant);
                continue;
            }
            if (role == "tool") {
                ToolResultMessage tool;
                tool.timestamp = UserMessage::now_ms();
                tool.tool_name = msg.value("name", std::string("tool"));
                tool.tool_call_id = msg.value("tool_call_id", std::string{});
                if (!text.empty()) {
                    tool.content.push_back(TextContent{.text = text});
                }
                ctx.messages.push_back(tool);
            }
        }
    }

    if (request.contains("prompt") && request["prompt"].is_string()) {
        ctx.messages.push_back(UserMessage::text(request["prompt"].get<std::string>()));
    }

    return ctx;
}

json message_json_from_assistant(const AssistantMessage& msg) {
    return json{
        {"role", "assistant"},
        {"text", text_of(msg.content)}
    };
}

json tool_calls_json_from_assistant(const AssistantMessage& msg) {
    json calls = json::array();
    for (const auto& item : msg.content) {
        if (const auto* tc = std::get_if<ToolCall>(&item)) {
            calls.push_back({
                {"id", tc->id},
                {"name", tc->name},
                {"arguments", tc->arguments}
            });
        }
    }
    return calls;
}

json usage_json_from_assistant(const AssistantMessage& msg) {
    return json{
        {"input_tokens", msg.usage.input},
        {"output_tokens", msg.usage.output},
        {"cache_read_tokens", msg.usage.cache_read},
        {"cache_write_tokens", msg.usage.cache_write}
    };
}

std::string normalized_finish_reason(StopReason reason) {
    switch (reason) {
        case StopReason::stop: return "stop";
        case StopReason::tool_use: return "tool_use";
        case StopReason::length: return "length";
        case StopReason::error: return "error";
        case StopReason::aborted: return "unknown";
    }
    return "unknown";
}

bool provider_enabled(const json& provider_block) {
    return !provider_block.is_object() || provider_block.value("enabled", true);
}

} // namespace

Runtime::Runtime()
    : config_(json::object()),
      last_error_(nullptr),
      last_trace_(nullptr),
      stream_manager_(std::make_shared<StreamManager>()) {
    register_builtin_providers_once();
}

Runtime::Runtime(const std::string& config_json) : Runtime() {
    configure_json(config_json);
}

void Runtime::configure_json(const std::string& config_json) {
    clear_error();
    try {
        if (config_json.empty()) {
            config_ = json::object();
        } else {
            json parsed = json::parse(config_json);
            if (!parsed.is_object()) {
                throw std::runtime_error("Runtime config must be a JSON object");
            }
            config_ = std::move(parsed);
        }
        last_trace_ = json{{"configured", true}};
    } catch (const std::exception& e) {
        set_error("config_invalid", e.what(), false);
        throw;
    }
}

void Runtime::configure_file(const std::string& config_path) {
    configure_json(read_file(config_path));
}

std::string Runtime::stream_request_json(const std::string& request_json_text) {
    clear_error();
    try {
        json request = json::parse(request_json_text.empty() ? "{}" : request_json_text);
        if (!request.is_object()) {
            throw std::runtime_error("Runtime stream request must be a JSON object");
        }
        if ((!request.contains("prompt") || !request["prompt"].is_string()) &&
            (!request.contains("messages") || !request["messages"].is_array())) {
            throw std::runtime_error("Stream request must provide prompt or messages");
        }

        const std::string stream_id = stream_manager_->createStream();
        auto manager = stream_manager_;

        // Unit-test/synthetic stream hook: exercises the same ghost-queue/sync path
        // without invoking a network provider.
        if (request.contains("stream_test_text") && request["stream_test_text"].is_string()) {
            const std::string text = request["stream_test_text"].get<std::string>();
            last_trace_ = json{{"stream_id", stream_id}, {"synthetic_stream", true}};
            manager->pushToken(stream_id, text);
            manager->markComplete(stream_id);
            return stream_id;
        }

        const std::string provider = resolve_provider(request);
        const std::string model = resolve_model(provider, request);
        const std::string api = resolve_api(provider);
        const std::string base_url = resolve_base_url(provider);
        last_trace_ = build_trace(provider, model, request);
        last_trace_["stream_id"] = stream_id;

        Model runtime_model{
            .id = model,
            .name = model,
            .api = api,
            .provider = provider,
            .base_url = base_url,
            .headers = {}
        };

        Context ctx = build_context_from_request(request);
        StreamOptions opts;
        const std::string api_key = resolve_api_key(provider, request);
        if (!api_key.empty()) {
            opts.api_key = api_key;
        }
        if (request.contains("options") && request["options"].is_object()) {
            const auto& options = request["options"];
            if (options.contains("max_output_tokens") && options["max_output_tokens"].is_number_integer()) {
                opts.max_tokens = options["max_output_tokens"].get<int>();
            }
            if (options.contains("temperature") && options["temperature"].is_number()) {
                opts.temperature = options["temperature"].get<double>();
            }
            if (options.contains("reasoning_effort") && options["reasoning_effort"].is_string()) {
                opts.reasoning_effort = options["reasoning_effort"].get<std::string>();
            }
        }

        auto provider_fn = agentc::runtime::get_provider(api);
        std::thread([manager,
                     stream_id,
                     runtime_model = std::move(runtime_model),
                     ctx = std::move(ctx),
                     opts = std::move(opts),
                     provider_fn = std::move(provider_fn)]() mutable {
            bool complete = false;
            bool failed = false;
            std::string emitted_text;

            const auto mark_complete_once = [&]() {
                if (!complete && !failed) {
                    complete = true;
                    manager->markComplete(stream_id);
                }
            };
            const auto mark_error_once = [&](const std::string& message) {
                if (!failed) {
                    failed = true;
                    manager->markError(stream_id, message);
                }
            };
            const auto emit_final_if_needed = [&](const AssistantMessage& msg) {
                const std::string final_text = text_of(msg.content);
                if (emitted_text.empty() && !final_text.empty()) {
                    emitted_text += final_text;
                    manager->pushToken(stream_id, final_text);
                }
            };

            AssistantMessageStream stream;
            stream.on_event([&](AssistantMessageEvent ev) {
                std::visit([&](auto&& e) {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, EvTextDelta>) {
                        if (!e.delta.empty()) {
                            emitted_text += e.delta;
                            manager->pushToken(stream_id, e.delta);
                        }
                    } else if constexpr (std::is_same_v<T, EvDone>) {
                        emit_final_if_needed(e.message);
                        mark_complete_once();
                    } else if constexpr (std::is_same_v<T, EvError>) {
                        std::string message = e.message.error_message.value_or("provider stream error");
                        mark_error_once(message);
                    }
                }, ev);
            });
            stream.on_complete([&](AssistantMessage msg) {
                emit_final_if_needed(msg);
                mark_complete_once();
            });
            stream.on_error([&](std::exception_ptr ep) {
                try {
                    if (ep) std::rethrow_exception(ep);
                } catch (const std::exception& e) {
                    mark_error_once(e.what());
                    return;
                } catch (...) {
                    mark_error_once("unknown provider stream error");
                    return;
                }
                mark_error_once("unknown provider stream error");
            });

            try {
                provider_fn(runtime_model, ctx, opts, stream);
                mark_complete_once();
            } catch (const std::exception& e) {
                mark_error_once(e.what());
            } catch (...) {
                mark_error_once("unknown provider stream exception");
            }
        }).detach();

        return stream_id;

    } catch (const json::parse_error& e) {
        set_error("parse_error", std::string("Invalid JSON stream request: ") + e.what(), false);
    } catch (const std::exception& e) {
        set_error("internal_error", std::string("Internal stream error: ") + e.what(), false);
    }
    return "";
}

json Runtime::request_json(const std::string& request_json_text) {
    clear_error();

    const auto error_response = [&](const std::string& code,
                                    const std::string& message,
                                    bool retryable,
                                    const json& provider_error = nullptr) {
        set_error(code, message, retryable, provider_error);
        json res = json{
            {"ok", false},
            {"request_id", nullptr},
            {"provider", nullptr},
            {"model", nullptr},
            {"finish_reason", "error"},
            {"message", nullptr},
            {"tool_calls", json::array()},
            {"usage", nullptr},
            {"error", last_error_},
            {"trace", last_trace_.is_null() ? nullptr : last_trace_},
            {"raw", nullptr}
        };
        std::cerr << "[Runtime] Error Response:\n" << res.dump(2) << std::endl;
        return res;
    };

    try {
        json request = json::parse(request_json_text.empty() ? "{}" : request_json_text);
        if (!request.is_object()) {
            return error_response("request_invalid", "Runtime request must be a JSON object", false);
        }

        if ((!request.contains("prompt") || !request["prompt"].is_string()) &&
            (!request.contains("messages") || !request["messages"].is_array())) {
            return error_response("request_invalid", "Request must provide prompt or messages", false);
        }

        const std::string provider = resolve_provider(request);
        const std::string model = resolve_model(provider, request);
        const std::string api = resolve_api(provider);
        const std::string base_url = resolve_base_url(provider);
        const std::string request_id = request.value("request_id", generated_request_id());

        last_trace_ = build_trace(provider, model, request);

        Model runtime_model{
            .id = model,
            .name = model,
            .api = api,
            .provider = provider,
            .base_url = base_url,
            .headers = {}
        };

        Context ctx = build_context_from_request(request);
        StreamOptions opts;
        const std::string api_key = resolve_api_key(provider, request);
        if (!api_key.empty()) {
            opts.api_key = api_key;
        }

        if (request.contains("options") && request["options"].is_object()) {
            const auto& options = request["options"];
            if (options.contains("max_output_tokens") && options["max_output_tokens"].is_number_integer()) {
                opts.max_tokens = options["max_output_tokens"].get<int>();
            }
            if (options.contains("temperature") && options["temperature"].is_number()) {
                opts.temperature = options["temperature"].get<double>();
            }
            if (options.contains("reasoning_effort") && options["reasoning_effort"].is_string()) {
                opts.reasoning_effort = options["reasoning_effort"].get<std::string>();
            }
        }

        AssistantMessage final_msg;
        bool got_final = false;
        std::string streamed_text;
        std::exception_ptr stream_error;

        std::cerr << "[Runtime] provider: " << provider << ", model: " << model << ", api: " << api << std::endl;
        auto provider_fn = agentc::runtime::get_provider(api);
        AssistantMessageStream stream;
        stream.on_event([&](AssistantMessageEvent ev) {
            std::visit([&](auto&& e) {
                using T = std::decay_t<decltype(e)>;
                if constexpr (std::is_same_v<T, EvTextDelta>) {
                    streamed_text += e.delta;
                } else if constexpr (std::is_same_v<T, EvDone>) {
                    final_msg = e.message;
                    got_final = true;
                }
            }, ev);
        });
        stream.on_complete([&](AssistantMessage msg) {
            final_msg = std::move(msg);
            got_final = true;
        });
        stream.on_error([&](std::exception_ptr ep) {
            stream_error = ep;
        });

        provider_fn(runtime_model, ctx, opts, stream);

        if (stream_error) {
            try {
                std::rethrow_exception(stream_error);
            } catch (const std::exception& e) {
                std::cerr << "[Runtime] Error Response (Exception): " << e.what() << std::endl;
                return error_response("provider_response_invalid", e.what(), false);
            }
        }

        if (!got_final) {
            if (!streamed_text.empty()) {
                final_msg.api = runtime_model.api;
                final_msg.provider = runtime_model.provider;
                final_msg.model_id = runtime_model.id;
                final_msg.stop_reason = StopReason::stop;
                final_msg.content.push_back(TextContent{.text = streamed_text});
                got_final = true;
            } else {
                return error_response("provider_response_invalid", "Provider returned no final response", false);
            }
        }

        if (final_msg.content.empty() && !streamed_text.empty()) {
            final_msg.content.push_back(TextContent{.text = streamed_text});
        }
        if (final_msg.provider.empty()) final_msg.provider = runtime_model.provider;
        if (final_msg.model_id.empty()) final_msg.model_id = runtime_model.id;
        if (final_msg.api.empty()) final_msg.api = runtime_model.api;

        json res = json{
            {"ok", true},
            {"request_id", request_id},
            {"provider", final_msg.provider},
            {"model", final_msg.model_id},
            {"finish_reason", normalized_finish_reason(final_msg.stop_reason)},
            {"message", message_json_from_assistant(final_msg)},
            {"tool_calls", tool_calls_json_from_assistant(final_msg)},
            {"usage", usage_json_from_assistant(final_msg)},
            {"error", nullptr},
            {"trace", last_trace_},
            {"raw", nullptr}
        };
        return res;
    } catch (const std::exception& e) {
        return error_response("internal_error", e.what(), false);
    }
}

std::string Runtime::last_error_json() const {
    return last_error_.is_null() ? std::string() : last_error_.dump();
}

std::string Runtime::last_trace_json() const {
    return last_trace_.is_null() ? std::string() : last_trace_.dump();
}

void Runtime::clear_error() {
    last_error_ = nullptr;
}

void Runtime::set_error(const std::string& code,
                        const std::string& message,
                        bool retryable,
                        const json& provider_error,
                        const json& context) {
    last_error_ = json{
        {"code", code},
        {"message", message},
        {"retryable", retryable},
        {"provider_error", provider_error.is_null() ? json(nullptr) : provider_error},
        {"context", context.is_null() ? json(nullptr) : context}
    };
}

std::string Runtime::resolve_provider(const json& request) const {
    std::string provider = request.value("provider", config_.value("default_provider", std::string{}));
    if (provider.empty()) {
        throw std::runtime_error("No provider specified and no default_provider configured");
    }

    const auto providers = config_.value("providers", json::object());
    if (providers.contains(provider) && !provider_enabled(providers[provider])) {
        throw std::runtime_error("Configured provider is disabled: " + provider);
    }
    return provider;
}

std::string Runtime::resolve_model(const std::string& provider, const json& request) const {
    if (request.contains("model") && request["model"].is_string()) {
        return request["model"].get<std::string>();
    }
    const auto providers = config_.value("providers", json::object());
    if (providers.contains(provider) && providers[provider].contains("default_model") && providers[provider]["default_model"].is_string()) {
        return providers[provider]["default_model"].get<std::string>();
    }
    const std::string configured_default = config_.value("default_model", std::string{});
    if (!configured_default.empty()) {
        return configured_default;
    }
    throw std::runtime_error("No model specified and no default model configured for provider: " + provider);
}

std::string Runtime::resolve_api(const std::string& provider) const {
    if (provider == "google") return "google-gemini-cli";
    if (provider == "openai") return "openai-completions";
    if (provider == "local") return "openai-completions";
    if (provider == "github-copilot") return "openai-completions";
    if (provider == "openai-codex") return "openai-codex-responses";
    throw std::runtime_error("Unsupported provider: " + provider);
}

std::string Runtime::resolve_base_url(const std::string& provider) const {
    const auto providers = config_.value("providers", json::object());
    if (providers.contains(provider) && providers[provider].contains("base_url") && providers[provider]["base_url"].is_string()) {
        return providers[provider]["base_url"].get<std::string>();
    }
    const std::string fallback = default_base_url_for_provider(provider);
    if (fallback.empty()) {
        throw std::runtime_error("No base_url configured for provider: " + provider);
    }
    return fallback;
}

std::string Runtime::resolve_api_key(const std::string& provider, const json& request) const {
    if (request.contains("options") && request["options"].is_object()) {
        const auto& options = request["options"];
        if (options.contains("api_key") && options["api_key"].is_string()) {
            return options["api_key"].get<std::string>();
        }
    }

    const auto providers = config_.value("providers", json::object());
    if (providers.contains(provider) && providers[provider].contains("api_key") && providers[provider]["api_key"].is_string()) {
        return providers[provider]["api_key"].get<std::string>();
    }

    if (provider == "google") {
        return get_google_api_key();
    }
    if (provider == "openai") {
        return get_openai_api_key();
    }
    if (provider == "openai-codex") {
        auto creds = get_openai_codex_credentials();
        return creds.access;
    }
    return {};
}

json Runtime::build_trace(const std::string& provider,
                          const std::string& model,
                          const json& request) const {
    return json{
        {"selected_provider", provider},
        {"selected_model", model},
        {"api", resolve_api(provider)},
        {"request_has_prompt", request.contains("prompt")},
        {"request_message_count", request.contains("messages") && request["messages"].is_array()
            ? static_cast<int>(request["messages"].size()) : 0}
    };
}

} // namespace agentc::runtime
