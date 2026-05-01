#include "socket_server.h"
#include <agentc_runtime/agentc_runtime.h>

#include <asio.hpp>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <vector>

namespace {

using json = nlohmann::json;

static bool safe_write(asio::local::stream_protocol::socket& socket, const std::string& data) {
    std::error_code ec;
    asio::write(socket, asio::buffer(data), ec);
    if (ec) {
        std::cerr << "socket write failed: " << ec.message() << std::endl;
        return false;
    }
    return true;
}

static std::string env_or_default(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

struct HostOptions {
    std::string socket_path = "/tmp/agentc.sock";
    std::string config_path;
    std::string provider = env_or_default("AGENTC_PROVIDER", "google");
    std::string model = env_or_default("AGENTC_MODEL", "gemini-2.5-pro");
    std::string system_prompt = env_or_default("AGENTC_SYSTEM_PROMPT", "You are a helpful coding assistant.");
};

static HostOptions parse_options(int argc, char** argv) {
    HostOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto require_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--socket") {
            options.socket_path = require_value("--socket");
        } else if (arg == "--config") {
            options.config_path = require_value("--config");
        } else if (arg == "--provider") {
            options.provider = require_value("--provider");
        } else if (arg == "--model") {
            options.model = require_value("--model");
        } else if (arg == "--system-prompt") {
            options.system_prompt = require_value("--system-prompt");
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }
    return options;
}

static json default_runtime_config(const HostOptions& options) {
    return json{
        {"default_provider", options.provider},
        {"default_model", options.model},
        {"defaults", {
            {"timeout_ms", 30000},
            {"include_raw", false},
            {"response_mode", "text"}
        }}
    };
}

static std::string runtime_call_json(agentc_runtime_t runtime, const json& request) {
    char* response = agentc_runtime_request_json(runtime, request.dump().c_str());
    if (!response) {
        throw std::runtime_error("agentc_runtime_request_json returned null");
    }
    std::string result(response);
    agentc_runtime_free_string(response);
    return result;
}

struct SessionState {
    std::string system_prompt;
    std::vector<json> messages;
};

static json build_request(const SessionState& session, const std::string& input) {
    return json{
        {"system", session.system_prompt},
        {"messages", session.messages},
        {"prompt", input},
        {"response_mode", "text"}
    };
}

static std::string extract_reply_text(const json& response) {
    if (!response.value("ok", false)) {
        if (response.contains("error") && response["error"].is_object()) {
            return std::string("[error] ") + response["error"].value("message", std::string("unknown runtime error"));
        }
        return "[error] request failed";
    }

    if (response.contains("message") && response["message"].is_object()) {
        const auto& message = response["message"];
        if (message.contains("text") && message["text"].is_string()) {
            const auto text = message["text"].get<std::string>();
            if (!text.empty()) {
                return text;
            }
        }
    }

    return "[no assistant text returned]";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::signal(SIGPIPE, SIG_IGN);

        const HostOptions options = parse_options(argc, argv);
        const auto config = default_runtime_config(options);
        agentc_runtime_t runtime = options.config_path.empty()
            ? agentc_runtime_create_json(config.dump().c_str())
            : agentc_runtime_create_file(options.config_path.c_str());
        if (!runtime) {
            std::cerr << "Failed to initialize agent runtime" << std::endl;
            return 1;
        }

        asio::io_context io_context;
        unlink(options.socket_path.c_str());
        SocketServer server(io_context, options.socket_path);

        std::printf("AgentC Daemon ready (runtime-backed mode). Listening on %s\n", options.socket_path.c_str());

        server.start_accept([&](asio::local::stream_protocol::socket socket) {
            SessionState session{.system_prompt = options.system_prompt};
            bool keep_server_running = true;
            std::printf("Client attached.\n");
            const std::string banner = "AgentC Session Attached.\n> ";
            if (!safe_write(socket, banner)) return true;

            asio::streambuf buffer;
            while (true) {
                std::error_code ec;
                asio::read_until(socket, buffer, '\n', ec);
                if (ec) break;

                std::istream is(&buffer);
                std::string input;
                std::getline(is, input);
                if (input == "exit") break;
                if (input == "shutdown-agent") {
                    safe_write(socket, "Agent: shutting down.\n");
                    keep_server_running = false;
                    break;
                }

                try {
                    const auto request = build_request(session, input);
                    const auto response_text = runtime_call_json(runtime, request);
                    const auto response = json::parse(response_text);
                    const auto reply_text = extract_reply_text(response);

                    session.messages.push_back(json{{"role", "user"}, {"text", input}});
                    if (response.value("ok", false) && response.contains("message") && response["message"].is_object()) {
                        session.messages.push_back(response["message"]);
                    }

                    const std::string reply = "Agent: " + reply_text + "\n> ";
                    if (!safe_write(socket, reply)) break;
                } catch (const std::exception& e) {
                    const std::string reply = std::string("Agent error: ") + e.what() + "\n> ";
                    if (!safe_write(socket, reply)) break;
                }
            }
            std::printf("Client detached.\n");
            return keep_server_running;
        });

        agentc_runtime_destroy(runtime);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "AgentC host error: " << e.what() << std::endl;
        return 1;
    }
}
