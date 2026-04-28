#pragma once
// ai_types.h — C++ port of ~/pi-mono/packages/ai/src/types.ts
// All types required by providers, agent loop, and tool dispatch.

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>

// ─── Content block types ────────────────────────────────────────────────────

struct TextContent {
    std::string type = "text";
    std::string text;
};

struct ThinkingContent {
    std::string type = "thinking";
    std::string thinking;
    std::optional<std::string> thinking_signature;
    bool redacted = false;
};

struct ToolCall {
    std::string type = "toolCall";
    std::string id;
    std::string name;
    nlohmann::json arguments;  // parsed JSON object
    std::optional<std::string> thought_signature;
};

struct ImageContent {
    std::string type = "image";
    std::string data;       // base64
    std::string mime_type;  // e.g. "image/png"
};

using AssistantContent = std::variant<TextContent, ThinkingContent, ToolCall>;
using UserContent      = std::variant<TextContent, ImageContent>;
using ToolContent      = std::variant<TextContent, ImageContent>;

// ─── Usage ──────────────────────────────────────────────────────────────────

struct UsageCost {
    double input       = 0;
    double output      = 0;
    double cache_read  = 0;
    double cache_write = 0;
    double total       = 0;
};

struct Usage {
    int input        = 0;
    int output       = 0;
    int cache_read   = 0;
    int cache_write  = 0;
    int total_tokens = 0;
    UsageCost cost;
};

// ─── Stop reason ────────────────────────────────────────────────────────────

enum class StopReason { stop, length, tool_use, error, aborted };

inline std::string stop_reason_str(StopReason r) {
    switch (r) {
        case StopReason::stop:     return "stop";
        case StopReason::length:   return "length";
        case StopReason::tool_use: return "toolUse";
        case StopReason::error:    return "error";
        case StopReason::aborted:  return "aborted";
    }
    return "stop";
}

// ─── Messages ───────────────────────────────────────────────────────────────

struct UserMessage {
    std::string role = "user";
    std::vector<UserContent> content;
    int64_t timestamp = 0;

    // Convenience: set plain text content
    static UserMessage text(const std::string& t) {
        UserMessage m;
        m.content.push_back(TextContent{.text = t});
        m.timestamp = now_ms();
        return m;
    }
    static int64_t now_ms();
};

struct AssistantMessage {
    std::string role     = "assistant";
    std::string api;
    std::string provider;
    std::string model_id;
    std::optional<std::string> response_id;
    std::vector<AssistantContent> content;
    Usage usage;
    StopReason stop_reason = StopReason::stop;
    std::optional<std::string> error_message;
    int64_t timestamp = 0;
};

struct ToolResultMessage {
    std::string role = "toolResult";
    std::string tool_call_id;
    std::string tool_name;
    std::vector<ToolContent> content;
    bool is_error = false;
    int64_t timestamp = 0;
};

using Message = std::variant<UserMessage, AssistantMessage, ToolResultMessage>;

// ─── Tool schema ─────────────────────────────────────────────────────────────

struct Tool {
    std::string name;
    std::string description;
    nlohmann::json parameters;  // JSON Schema object
};

// ─── Context ──────────────────────────────────────────────────────────────

struct Context {
    std::optional<std::string> system_prompt;
    std::vector<Message> messages;
    std::vector<Tool> tools;
};

// ─── Model ───────────────────────────────────────────────────────────────────

struct Model {
    std::string id;
    std::string name;
    std::string api;          // e.g. "google-gemini-cli", "openai-completions"
    std::string provider;     // e.g. "google", "openai", "github-copilot"
    std::string base_url;
    bool reasoning = false;
    std::unordered_map<std::string, std::string> headers;  // static per-provider headers
    int context_window = 128000;
    int max_tokens     = 8192;
};

// ─── Stream options ──────────────────────────────────────────────────────────

struct StreamOptions {
    std::optional<std::string> api_key;
    std::optional<int> max_tokens;
    std::optional<double> temperature;
    std::optional<std::string> session_id;
    std::unordered_map<std::string, std::string> extra_headers;
};

// ─── Streaming event types (AssistantMessageEvent) ───────────────────────────

struct EvStart          { AssistantMessage partial; };
struct EvTextStart      { int content_index; AssistantMessage partial; };
struct EvTextDelta      { int content_index; std::string delta; AssistantMessage partial; };
struct EvTextEnd        { int content_index; std::string text; AssistantMessage partial; };
struct EvThinkingStart  { int content_index; AssistantMessage partial; };
struct EvThinkingDelta  { int content_index; std::string delta; AssistantMessage partial; };
struct EvThinkingEnd    { int content_index; std::string thinking; AssistantMessage partial; };
struct EvToolCallStart  { int content_index; AssistantMessage partial; };
struct EvToolCallDelta  { int content_index; std::string delta; AssistantMessage partial; };
struct EvToolCallEnd    { int content_index; ToolCall tool_call; AssistantMessage partial; };
struct EvDone           { StopReason reason; AssistantMessage message; };
struct EvError          { StopReason reason; AssistantMessage message; };

using AssistantMessageEvent = std::variant<
    EvStart, EvTextStart, EvTextDelta, EvTextEnd,
    EvThinkingStart, EvThinkingDelta, EvThinkingEnd,
    EvToolCallStart, EvToolCallDelta, EvToolCallEnd,
    EvDone, EvError
>;

// ─── EventStream ─────────────────────────────────────────────────────────────

#include <functional>
#include <stdexcept>

template<typename TEvent, typename TResult>
class EventStream {
public:
    using EmitFn    = std::function<void(TEvent)>;
    using CompleteFn = std::function<void(TResult)>;
    using ErrorFn   = std::function<void(std::exception_ptr)>;

    void on_event(EmitFn fn)      { emit_fn_    = std::move(fn); }
    void on_complete(CompleteFn fn){ complete_fn_ = std::move(fn); }
    void on_error(ErrorFn fn)     { error_fn_   = std::move(fn); }

    void push(TEvent ev) {
        if (emit_fn_) emit_fn_(std::move(ev));
    }
    void end(TResult result) {
        if (complete_fn_) complete_fn_(std::move(result));
    }
    void fail(std::exception_ptr ep) {
        if (error_fn_) error_fn_(ep);
    }

private:
    EmitFn     emit_fn_;
    CompleteFn complete_fn_;
    ErrorFn    error_fn_;
};

using AssistantMessageStream = EventStream<AssistantMessageEvent, AssistantMessage>;

// ─── StreamFn type ───────────────────────────────────────────────────────────

using StreamFn = std::function<void(
    const Model&,
    const Context&,
    const StreamOptions&,
    AssistantMessageStream&
)>;

// ─── Inline helpers ─────────────────────────────────────────────────────────

#include <chrono>
inline int64_t UserMessage::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Convenience: extract text from a message's content
inline std::string text_of(const std::vector<AssistantContent>& content) {
    std::string out;
    for (auto& c : content)
        if (auto* t = std::get_if<TextContent>(&c)) out += t->text;
    return out;
}

inline std::string text_of(const std::vector<UserContent>& content) {
    std::string out;
    for (auto& c : content)
        if (auto* t = std::get_if<TextContent>(&c)) out += t->text;
    return out;
}

// ToolContent = same variant as UserContent — use a distinct name to avoid collision
inline std::string text_of_tool(const std::vector<ToolContent>& content) {
    std::string out;
    for (auto& c : content)
        if (auto* t = std::get_if<TextContent>(&c)) out += t->text;
    return out;
}
