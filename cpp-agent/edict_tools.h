#pragma once
#include "agent_types.h"
#include <string>
#include <vector>

// Edict-backed tool registry.
// Tools are populated by loading bundle scripts at session start.
// The VM dictionary IS the tool registry — no C++ per tool.

class EdictToolRegistry {
public:
    EdictToolRegistry();
    ~EdictToolRegistry();

    // Load a bundle Edict script (calls resolver.import ! etc.)
    void load_bundle(const std::string& bundle_path);

    // Walk VM dictionary, return Tool vector for LLM
    std::vector<AgentTool> get_tools() const;

    // Execute a tool call: construct "{args} name !" → vm.execute()
    AgentToolResult execute_tool(
        const std::string& tool_call_id,
        const std::string& name,
        const nlohmann::json& args
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
