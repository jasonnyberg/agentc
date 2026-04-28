#include "agent_loop.h"
#include <stdexcept>

// ─── Forward declarations ────────────────────────────────────────────────────

static void run_loop(AgentContext& ctx, std::vector<AgentMessage>& new_msgs,
                     const AgentLoopConfig& cfg, AgentEventSink& emit);

static std::vector<ToolResultMessage> execute_tool_calls(
    const AgentContext& ctx,
    const AssistantMessage& assistant,
    const AgentLoopConfig& cfg,
    AgentEventSink& emit);

// ─── Emit helper (null-safe) ─────────────────────────────────────────────────

static void emit(AgentEventSink& sink, AgentEvent ev) {
    if (sink) sink(std::move(ev));
}

// ─── Main entry ─────────────────────────────────────────────────────────────

std::vector<AgentMessage> run_agent_loop(
    const std::vector<AgentMessage>& prompts,
    AgentContext& context,
    const AgentLoopConfig& config,
    AgentEventSink emit_fn)
{
    std::vector<AgentMessage> new_msgs(prompts.begin(), prompts.end());
    for (auto& p : prompts) context.messages.push_back(p);

    ::emit(emit_fn, EvAgentStart{});
    ::emit(emit_fn, EvTurnStart{});
    for (auto& p : prompts) {
        ::emit(emit_fn, EvMessageStart{p});
        ::emit(emit_fn, EvMessageEnd{p});
    }

    run_loop(context, new_msgs, config, emit_fn);

    ::emit(emit_fn, EvTurnEnd{});
    ::emit(emit_fn, EvAgentEnd{new_msgs});
    return new_msgs;
}

// ─── Core loop ───────────────────────────────────────────────────────────────

static void run_loop(AgentContext& ctx, std::vector<AgentMessage>& new_msgs,
                     const AgentLoopConfig& cfg, AgentEventSink& emit_fn)
{
    if (!cfg.stream_fn)
        throw std::runtime_error("AgentLoopConfig: stream_fn is not set");

    int iterations = 0;
    const int max_iter = cfg.max_iterations > 0 ? cfg.max_iterations : 10;

    while (iterations++ < max_iter) {
        // Build Context for LLM call
        Context llm_ctx;
        llm_ctx.system_prompt = ctx.system_prompt;
        llm_ctx.messages      = ctx.messages;
        for (auto& t : ctx.tools)
            llm_ctx.tools.push_back(Tool{t.name, t.description, t.parameters});

        // Call the LLM provider via stream function
        AssistantMessage final_msg;
        bool got_final = false;

        AssistantMessageStream stream;

        stream.on_event([&](AssistantMessageEvent ev) {
            // Forward partial updates as agent message-update events
            std::visit([&](auto& e) {
                using T = std::decay_t<decltype(e)>;
                // Types with a 'partial' field
                if constexpr (
                    std::is_same_v<T, EvStart>        ||
                    std::is_same_v<T, EvTextStart>    ||
                    std::is_same_v<T, EvTextDelta>    ||
                    std::is_same_v<T, EvTextEnd>      ||
                    std::is_same_v<T, EvThinkingStart>||
                    std::is_same_v<T, EvThinkingDelta>||
                    std::is_same_v<T, EvThinkingEnd>  ||
                    std::is_same_v<T, EvToolCallStart>||
                    std::is_same_v<T, EvToolCallDelta>||
                    std::is_same_v<T, EvToolCallEnd>
                ) {
                    ::emit(emit_fn, EvMessageUpdate{ev, e.partial});
                }
                // EvDone / EvError carry a final 'message' not 'partial'
            }, ev);
        });

        stream.on_complete([&](AssistantMessage msg) {
            final_msg = std::move(msg);
            got_final = true;
        });

        stream.on_error([&](std::exception_ptr ep) {
            std::rethrow_exception(ep);
        });

        cfg.stream_fn(cfg.model, llm_ctx, StreamOptions{}, stream);

        if (!got_final)
            throw std::runtime_error("Provider stream completed without a final message");

        // Record the assistant message
        AgentMessage assistant_agent_msg = final_msg;
        ctx.messages.push_back(assistant_agent_msg);
        new_msgs.push_back(assistant_agent_msg);
        ::emit(emit_fn, EvMessageStart{assistant_agent_msg});
        ::emit(emit_fn, EvMessageEnd{assistant_agent_msg});

        // If stop reason is not tool_use, we're done
        if (final_msg.stop_reason != StopReason::tool_use)
            break;

        // Execute tool calls
        auto tool_results = execute_tool_calls(ctx, final_msg, cfg, emit_fn);

        // If all tools returned terminate=true, stop
        bool should_terminate = !tool_results.empty();
        for (auto& tr : tool_results) {
            AgentMessage tr_msg = tr;
            ctx.messages.push_back(tr_msg);
            new_msgs.push_back(tr_msg);
            ::emit(emit_fn, EvToolResult{tr});
        }
        if (should_terminate) {
            // Check if any tool signalled terminate (handled via tool result content marker)
            // For now, continue the loop — the LLM decides when to stop
        }
    }
}

// ─── Tool execution ──────────────────────────────────────────────────────────

static std::vector<ToolResultMessage> execute_tool_calls(
    const AgentContext& ctx,
    const AssistantMessage& assistant,
    const AgentLoopConfig& cfg,
    AgentEventSink& emit_fn)
{
    std::vector<ToolResultMessage> results;

    for (auto& content_block : assistant.content) {
        auto* tc = std::get_if<ToolCall>(&content_block);
        if (!tc) continue;

        ::emit(emit_fn, EvToolExecStart{tc->id, tc->name, tc->arguments});

        // Find the tool
        const AgentTool* tool = nullptr;
        for (auto& t : ctx.tools) {
            if (t.name == tc->name) { tool = &t; break; }
        }

        AgentToolResult result;
        if (!tool) {
            result.is_error = true;
            result.content.push_back(TextContent{.text = "Tool not found: " + tc->name});
        } else {
            try {
                result = tool->execute(tc->id, tc->arguments);
            } catch (std::exception& e) {
                result.is_error = true;
                result.content.push_back(TextContent{.text = std::string("Tool error: ") + e.what()});
            }
        }

        ::emit(emit_fn, EvToolExecEnd{tc->id, tc->name, result, result.is_error});

        ToolResultMessage tr;
        tr.tool_call_id = tc->id;
        tr.tool_name    = tc->name;
        tr.content      = result.content;
        tr.is_error     = result.is_error;
        tr.timestamp    = UserMessage::now_ms();
        results.push_back(tr);
    }

    return results;
}
