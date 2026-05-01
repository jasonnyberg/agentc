#include "socket_server.h"
#include "runtime/persistence/agent_root_state.h"
#include "runtime/persistence/session_state_store.h"
#include "../edict/edict_vm.h"
#include <agentc_runtime/agentc_runtime.h>

#include <asio.hpp>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>

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
    std::string state_base = env_or_default("AGENTC_STATE_BASE", "/tmp/agentc_session_state");
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
        } else if (arg == "--state-base") {
            options.state_base = require_value("--state-base");
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

static json runtime_config_from_agent_root(const json& root, const HostOptions& options) {
    json config = default_runtime_config(options);
    if (root.contains("runtime") && root["runtime"].is_object()) {
        const auto& runtime = root["runtime"];
        if (runtime.contains("default_provider") && runtime["default_provider"].is_string()) {
            config["default_provider"] = runtime["default_provider"];
        }
        if (runtime.contains("default_model") && runtime["default_model"].is_string()) {
            config["default_model"] = runtime["default_model"];
        }
    }
    return config;
}

static void configure_runtime_or_throw(agentc_runtime_t runtime, const json& config) {
    if (agentc_runtime_configure_json(runtime, config.dump().c_str()) == 0) {
        return;
    }

    char* error = agentc_runtime_last_error_json(runtime);
    std::string message = "agentc_runtime_configure_json failed";
    if (error) {
        message += ": ";
        message += error;
        agentc_runtime_free_string(error);
    }
    throw std::runtime_error(message);
}

static CPtr<agentc::ListreeValue> materialize_root_value_or_throw(const json& root) {
    auto value = agentc::fromJson(root.dump());
    if (!value) {
        throw std::runtime_error("failed to materialize canonical agent root into Listree");
    }
    return value;
}

static json root_json_from_vm_or_throw(agentc::edict::EdictVM& vm) {
    auto root = vm.getCursor().getValue();
    if (!root) {
        throw std::runtime_error("embedded VM has null root");
    }
    return json::parse(agentc::toJson(root));
}

static void replace_vm_root_or_throw(agentc::edict::EdictVM& vm, const json& root) {
    vm.setCursor(materialize_root_value_or_throw(root));
    vm.reset();
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

        agentc::runtime::SessionStateStore session_store(options.state_base);
        CPtr<agentc::ListreeValue> initial_root_value;
        if (session_store.exists()) {
            std::string error;
            if (session_store.loadRoot(initial_root_value, &error)) {
                std::printf("Restored persisted agent root from %s\n", options.state_base.c_str());
            } else {
                std::fprintf(stderr, "Warning: failed to restore agent root: %s\n", error.c_str());
            }
        }
        if (!initial_root_value) {
            initial_root_value = materialize_root_value_or_throw(
                agentc::runtime::make_default_agent_root(options.system_prompt, options.provider, options.model));
        }
        agentc::edict::EdictVM embedded_vm(initial_root_value);
        json shared_root = agentc::runtime::normalize_agent_root(
            root_json_from_vm_or_throw(embedded_vm), options.system_prompt, options.provider, options.model);
        replace_vm_root_or_throw(embedded_vm, shared_root);
        configure_runtime_or_throw(runtime, runtime_config_from_agent_root(shared_root, options));

        asio::io_context io_context;
        unlink(options.socket_path.c_str());
        SocketServer server(io_context, options.socket_path);

        std::printf("AgentC Daemon ready (runtime-backed mode). Listening on %s\n", options.socket_path.c_str());

        server.start_accept([&](asio::local::stream_protocol::socket socket) {
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
                if (input == "reset-session") {
                    shared_root = agentc::runtime::make_default_agent_root(options.system_prompt, options.provider, options.model);
                    replace_vm_root_or_throw(embedded_vm, shared_root);
                    configure_runtime_or_throw(runtime, runtime_config_from_agent_root(shared_root, options));
                    session_store.clear();
                    if (!safe_write(socket, "Agent: session reset.\n> ")) break;
                    continue;
                }
                if (input == "shutdown-agent") {
                    safe_write(socket, "Agent: shutting down.\n");
                    keep_server_running = false;
                    break;
                }

                try {
                    const auto request = agentc::runtime::build_request_from_agent_root(shared_root, input);
                    const auto response_text = runtime_call_json(runtime, request);
                    const auto response = json::parse(response_text);
                    const auto reply_text = extract_reply_text(response);

                    agentc::runtime::apply_runtime_response_to_agent_root(shared_root, input, response);
                    replace_vm_root_or_throw(embedded_vm, shared_root);

                    std::string persist_error;
                    if (!session_store.saveRoot(embedded_vm.getCursor().getValue(), &persist_error)) {
                        std::fprintf(stderr, "Warning: failed to persist agent root: %s\n", persist_error.c_str());
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
