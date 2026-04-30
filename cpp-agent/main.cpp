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
    register_google_provider();
    register_openai_provider();

    AgentContext context;
    context.system_prompt = "You are a helpful coding assistant.";

    EdictToolRegistry edict_tools;
    edict_tools.load_bundle("/tmp/test_tool.edict");
    context.tools = edict_tools.get_tools();

    // Use actual provider: Google Gemini
    AgentLoopConfig config;
    try {
        config.stream_fn = get_provider("google-gemini-cli");
    } catch (const std::exception& e) {
        printf("Error loading provider: %s\n", e.what());
        return 1;
    }
    config.model = Model{
        .id = "gemini-2.0-flash",
        .api = "google-gemini-cli",
        .provider = "google",
        .base_url = "https://generativelanguage.googleapis.com"
    };

    asio::io_context io_context;
    unlink("/tmp/agentc.sock");
    SocketServer server(io_context, "/tmp/agentc.sock");

    printf("AgentC Daemon ready (Gemini Mode). Listening on /tmp/agentc.sock\n");

    server.start_accept([&](asio::local::stream_protocol::socket socket) {
        asio::streambuf buffer;
        while(true) {
            std::error_code ec;
            asio::read_until(socket, buffer, '\n', ec);
            if (ec) break;

            std::istream is(&buffer);
            std::string input;
            std::getline(is, input);
            
            context.messages.push_back(UserMessage::text(input));
            
            StreamOptions opts;
            opts.api_key = get_google_api_key(); // Fetch from env
            
            auto messages = run_agent_loop({UserMessage::text(input)}, context, config, [&](AgentEvent ev) {});
            
            std::string response = "Agent: ";
            if (!context.messages.empty()) {
                const auto& last_msg = context.messages.back();
                if (auto* assistant_msg = std::get_if<AssistantMessage>(&last_msg)) {
                    response += text_of(assistant_msg->content);
                }
            }
            asio::write(socket, asio::buffer(response + "\n> "));
        }
    });
    return 0;
}
