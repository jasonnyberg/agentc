#include "agent_loop.h"
#include "agent_types.h"
#include "api_registry.h"
#include "edict_tools.h"
#include "credentials.h"
#include "providers/google.h"
#include "providers/openai.h"
#include "socket_server.h"
#include <iostream>
#include <asio.hpp>

int main() {
    AgentContext context;
    EdictToolRegistry edict_tools;
    edict_tools.load_bundle("/tmp/test_vocab.edict");
    context.tools = edict_tools.get_tools();

    // Mock stream for the two-turn sequence
    AgentLoopConfig config;
    config.stream_fn = [](const Model&, const Context& ctx, const StreamOptions&, AssistantMessageStream& stream) {
        AssistantMessage msg;
        // Turn 1: request definition
        if (ctx.messages.size() == 1) {
            msg.content.push_back(TextContent{.text = "Defined '123' as my_var"});
        } else {
            // Turn 2: recall and dereference
            msg.content.push_back(TextContent{.text = "123"});
        }
        stream.end(msg);
    };
    config.model = Model{.id = "test-model"};

    // Perform two turns
    for (int i=0; i<2; ++i) {
        run_agent_loop({UserMessage::text("turn")}, context, config, [&](AgentEvent ev) {});
    }

    // Verify result
    const auto& last_msg = std::get<AssistantMessage>(context.messages.back());
    std::cout << "Final Agent Response: " << text_of(last_msg.content) << std::endl;
    
    return 0;
}
