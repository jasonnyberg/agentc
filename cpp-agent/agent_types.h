#pragma once
// agent_types.h — C++ port of ~/pi-mono/packages/agent/src/types.ts

#include "ai_types.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ─── Agent tool result ───────────────────────────────────────────────────────

struct AgentToolResult {
    std::vector<ToolContent> content;
    nlohmann::json details;   // optional metadata
    bool is_error  = false;
    bool terminate = false;   // if true, stop the agent loop after this tool batch
};

// ─── Agent tool call (as received from LLM) ─────────────────────────────────

struct AgentToolCall {
    std::string id;
    std::string name;
    nlohmann::json arguments;
};

// ─── Agent tool definition ───────────────────────────────────────────────────

enum class ExecutionMode { parallel, sequential };

struct AgentTool {
    std::string    name;
    std::string    description;
    nlohmann::json parameters;   // JSON Schema
    ExecutionMode  execution_mode = ExecutionMode::parallel;

    // The actual handler — receives (tool_call_id, args) → result
    std::function<AgentToolResult(
        const std::string& tool_call_id,
        const nlohmann::json& args
    )> execute;
};

// ─── Agent message (union of all message roles + internal events) ─────────────

using AgentMessage = Message;  // same as ai_types Message

// ─── Agent context ───────────────────────────────────────────────────────────

struct AgentContext {
    std::optional<std::string> system_prompt;
    std::vector<AgentMessage>  messages;
    std::vector<AgentTool>     tools;
};

// ─── Agent loop configuration ────────────────────────────────────────────────

enum class ToolExecutionMode { parallel, sequential };

struct AgentLoopConfig {
    int               max_iterations    = 10;
    ToolExecutionMode tool_execution    = ToolExecutionMode::parallel;
    StreamFn          stream_fn;       // the LLM provider to use
    Model             model;           // model to call
};

// ─── Agent events ─────────────────────────────────────────────────────────────

struct EvAgentStart     {};
struct EvAgentEnd       { std::vector<AgentMessage> messages; };
struct EvTurnStart      {};
struct EvTurnEnd        {};
struct EvMessageStart   { AgentMessage message; };
struct EvMessageUpdate  { AssistantMessageEvent assistant_event; AgentMessage message; };
struct EvMessageEnd     { AgentMessage message; };
struct EvToolExecStart  { std::string tool_call_id; std::string tool_name; nlohmann::json args; };
struct EvToolExecEnd    { std::string tool_call_id; std::string tool_name; AgentToolResult result; bool is_error; };
struct EvToolResult     { ToolResultMessage message; };

using AgentEvent = std::variant<
    EvAgentStart, EvAgentEnd,
    EvTurnStart, EvTurnEnd,
    EvMessageStart, EvMessageUpdate, EvMessageEnd,
    EvToolExecStart, EvToolExecEnd, EvToolResult
>;

using AgentEventSink = std::function<void(AgentEvent)>;
using AgentStream    = EventStream<AgentEvent, std::vector<AgentMessage>>;
