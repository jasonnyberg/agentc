#pragma once
#include "agent_types.h"

// Run the agent loop with a new user prompt.
// Calls emit_fn for each AgentEvent as it occurs.
// Returns all new messages added this turn.
std::vector<AgentMessage> run_agent_loop(
    const std::vector<AgentMessage>& prompts,
    AgentContext& context,
    const AgentLoopConfig& config,
    AgentEventSink emit_fn = nullptr
);
