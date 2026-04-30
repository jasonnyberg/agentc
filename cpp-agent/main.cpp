#include "agent_loop.h"
#include "agent_types.h"
#include "api_registry.h"
#include "edict_tools.h"
#include "credentials.h"
#include "providers/google.h"
#include "providers/openai.h"
#include "socket_server.h"
#include <iostream>
#include <system_error>
#include <csignal>
#include <asio.hpp>

static bool safe_write(asio::local::stream_protocol::socket& socket, const std::string& data) {
    std::error_code ec;
    asio::write(socket, asio::buffer(data), ec);
    if (ec) {
        std::cerr << "socket write failed: " << ec.message() << std::endl;
        return false;
    }
    return true;
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    register_google_provider();
    register_openai_provider();

    AgentContext context;
    context.system_prompt = "You are a helpful coding assistant.";

    EdictToolRegistry edict_tools;
    edict_tools.load_bundle("/tmp/test_tool.edict");
    context.tools = edict_tools.get_tools();

    AgentLoopConfig config;
    try {
        config.stream_fn = get_provider("google-gemini-cli");
    } catch (...) { return 1; }
    
    config.model = Model{
        .id = "gemini-3.1-flash-lite-preview",
        .name = "Gemini Flash Lite",
        .api = "google-gemini-cli",
        .provider = "google",
        .base_url = "https://generativelanguage.googleapis.com",
        .headers = {}
    };

    asio::io_context io_context;
    unlink("/tmp/agentc.sock");
    SocketServer server(io_context, "/tmp/agentc.sock");

    printf("AgentC Daemon ready (Gemini 3.1 Mode). Listening on /tmp/agentc.sock\n");

    server.start_accept([&](asio::local::stream_protocol::socket socket) {
        printf("Client attached.\n");
        const std::string banner = "AgentC Session Attached.\n> ";
        if (!safe_write(socket, banner)) return;

        asio::streambuf buffer;
        while(true) {
            std::error_code ec;
            asio::read_until(socket, buffer, '\n', ec);
            if (ec) break;

            std::istream is(&buffer);
            std::string input;
            std::getline(is, input);
            if (input == "exit") break;

            std::string streamed_output;
            try {
                auto new_messages = run_agent_loop({UserMessage::text(input)}, context, config, [&](AgentEvent ev) {
                    if (const auto* e = std::get_if<EvMessageUpdate>(&ev)) {
                        if (const auto* assistant_msg = std::get_if<AssistantMessage>(&e->message)) {
                            const std::string text = text_of(assistant_msg->content);
                            if (!text.empty()) {
                                streamed_output = text;
                            }
                        }
                    }
                });

                if (streamed_output.empty()) {
                    for (const auto& msg : new_messages) {
                        if (const auto* assistant_msg = std::get_if<AssistantMessage>(&msg)) {
                            streamed_output = text_of(assistant_msg->content);
                        }
                    }
                }

                if (streamed_output.empty()) {
                    streamed_output = "[no assistant text returned]";
                }

                const std::string reply = "Agent: " + streamed_output + "\n> ";
                if (!safe_write(socket, reply)) break;
            } catch (const std::exception& e) {
                const std::string reply = std::string("Agent error: ") + e.what() + "\n> ";
                if (!safe_write(socket, reply)) break;
            }
        }
        printf("Client detached.\n");
    });
    return 0;
}
