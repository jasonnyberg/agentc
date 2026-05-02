#include "socket_server.h"
#include "runtime/persistence/agent_root_state.h"
#include "runtime/persistence/agent_root_vm_ops.h"
#include "runtime/persistence/session_state_store.h"
#include "../edict/edict_vm.h"

#include <asio.hpp>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    std::string config_path = env_or_default("AGENTC_CONFIG", "");
    std::string state_base = env_or_default("AGENTC_STATE_BASE", "/tmp/agentc_session_state");
    std::string session_name = env_or_default("AGENTC_SESSION", "default");
    std::string provider = env_or_default("AGENTC_PROVIDER", "google");
    std::string model = env_or_default("AGENTC_MODEL", "gemini-2.5-flash");
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
        } else if (arg == "--session") {
            options.session_name = require_value("--session");
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

static std::string resolve_runtime_config_path(const HostOptions& options) {
    if (!options.config_path.empty()) {
        return options.config_path;
    }
    if (std::filesystem::exists("agentc-config.json")) {
        return "agentc-config.json";
    }
    return {};
}

static json read_json_file_or_throw(const std::string& path) {
    std::ifstream in(path);
    if (!in.good()) {
        throw std::runtime_error("failed to open runtime config file: " + path);
    }
    return json::parse(in);
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

} // namespace

int main(int argc, char** argv) {
    try {
        std::signal(SIGPIPE, SIG_IGN);

        const HostOptions options = parse_options(argc, argv);
        const auto config_path = resolve_runtime_config_path(options);
        const json base_runtime_config = config_path.empty()
            ? default_runtime_config(options)
            : read_json_file_or_throw(config_path);
        const auto runtime_import_artifacts = agentc::runtime::discover_vm_runtime_import_artifacts();

        agentc::runtime::SessionStateStore session_store(options.state_base, options.session_name);
        CPtr<agentc::ListreeValue> initial_root_value;
        if (session_store.exists()) {
            std::string error;
            if (session_store.loadRoot(initial_root_value, &error)) {
                std::printf("Restored persisted agent root from %s/%s\n",
                            options.state_base.c_str(), options.session_name.c_str());
            } else {
                std::fprintf(stderr, "Warning: failed to restore agent root: %s\n", error.c_str());
            }
        }
        if (!initial_root_value) {
            initial_root_value = materialize_root_value_or_throw(
                agentc::runtime::make_default_agent_root(options.system_prompt, options.provider, options.model));
        }
        agentc::edict::EdictVM embedded_vm(initial_root_value);
        json normalized_root = agentc::runtime::normalize_agent_root(
            root_json_from_vm_or_throw(embedded_vm), options.system_prompt, options.provider, options.model);
        replace_vm_root_or_throw(embedded_vm, normalized_root);

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
                    const auto reset_root = agentc::runtime::make_default_agent_root(
                        options.system_prompt, options.provider, options.model);
                    replace_vm_root_or_throw(embedded_vm, reset_root);
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
                    agentc::runtime::run_vm_agent_root_turn_via_imported_runtime(
                        embedded_vm,
                        input,
                        base_runtime_config,
                        runtime_import_artifacts);
                    const auto reply_text = agentc::runtime::reply_text_from_vm_agent_root(embedded_vm);

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

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "AgentC host error: " << e.what() << std::endl;
        return 1;
    }
}
