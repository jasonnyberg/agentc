#include "agent_loop.h"
#include "agent_types.h"
#include "api_registry.h"
#include "edict_tools.h"
#include "credentials.h"
#include "providers/google.h"
#include "providers/openai.h"
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>

int main(int argc, char* argv[]) {
    printf("cpp-agent: G063 — AgentC Interactive UI\n");

    // Initialize Providers
    register_google_provider();
    register_openai_provider();

    std::string provider_name = (argc > 1) ? argv[1] : "simulation";
    printf("Selected Provider: %s\n", provider_name.c_str());

    AgentContext context;
    context.system_prompt = "You are a helpful coding assistant.";

    EdictToolRegistry edict_tools;
    edict_tools.load_bundle("/tmp/test_tool.edict");
    context.tools = edict_tools.get_tools();

    AgentLoopConfig config;
    if (provider_name != "simulation") {
        config.stream_fn = get_provider(provider_name);
        config.model = Model{.id = (provider_name == "google-gemini-cli" ? "gemini-2.0-flash" : "gpt-4o")};
    } else {
        config.stream_fn = [](const Model&, const Context&, const StreamOptions&, AssistantMessageStream& stream) {
            AssistantMessage msg;
            msg.content.push_back(TextContent{.text = "Simulation: Provider OK."});
            stream.end(msg);
        };
        config.model = Model{.id = "simulation"};
    }

    // Simple REPL for the UI
    std::string input;
    printf("Agent ready. Type 'exit' to quit.\n> ");
    while (std::getline(std::cin, input) && input != "exit") {
        context.messages.push_back(UserMessage::text(input));
        
        auto messages = run_agent_loop({UserMessage::text(input)}, context, config, [](AgentEvent ev) {
            // Simplified print
        });
        
        // Print the last message from the assistant
        if (context.messages.size() > 0) {
            const auto& last_msg = context.messages.back();
            if (auto* assistant_msg = std::get_if<AssistantMessage>(&last_msg)) {
                printf("Agent: %s\n", text_of(assistant_msg->content).c_str());
            }
        }
        
        printf("\n> ");
    }

    return 0;
}
