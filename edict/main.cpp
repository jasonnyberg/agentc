// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// AgentC is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with AgentC. If not, see <https://www.gnu.org/licenses/>.

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "edict_repl.h"
#include "edict_compiler.h"
#include "edict_vm.h"
#include "../core/alloc.h"
#include "../core/debug.h"
#include "../cpp-agent/runtime/persistence/session_state_store.h"

namespace {

bool startupTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("AGENTC_EDICT_TRACE_STARTUP");
        return value && *value && std::string(value) != "0";
    }();
    return enabled;
}

void startupTrace(const std::string& marker) {
    if (startupTraceEnabled()) {
        std::cerr << "EDICT-STARTUP: " << marker << std::endl;
    }
}

struct StartupOptions {
    bool help = false;
    bool sessionEnabled = false;
    std::string sessionId;
    std::string sessionBase;
    int commandIndex = 1;
};

struct StartupSession {
    bool enabled = false;
    bool restored = false;
    std::string sessionId;
    std::string sessionBase;
    std::unique_ptr<agentc::runtime::SessionStateStore> store;
};

std::string envOrDefault(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

bool isSafeSessionId(const std::string& value) {
    if (value.empty() || value == "." || value == "..") {
        return false;
    }
    for (unsigned char ch : value) {
        if (!(std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')) {
            return false;
        }
    }
    return true;
}

void stripVolatileStartupBindings(CPtr<agentc::ListreeValue> root) {
    if (!root || root->isListMode()) {
        return;
    }

    // Session persistence currently stores the Edict root scope after startup
    // builtins have been injected.  Some of those builtins are bytecode/binary
    // thunks, which are intentionally lossy through the current session-image
    // snapshot path.  Strip the VM-owned startup surface before constructing a
    // new VM so loadBuiltins()/the bootstrap prelude can reinstall fresh thunks
    // without stale null histories shadowing them.
    static const char* const names[] = {
        "reset", "yield", "dup", "swap", "ref", "assign", "remove", "remove_head",
        "!", ".", "print", "fail", "test", "lax", "strict", "strict_null", "strict_fail",
        "ffi_closure", "rewrite_define", "rewrite_list", "rewrite_remove", "rewrite_apply",
        "rewrite_mode", "rewrite_trace", "speculate", "unsafe_extensions_allow",
        "unsafe_extensions_block", "unsafe_extensions_status", "HeapUtilization", "freeze",
        "to_json", "from_json", "intern_run", "intern_start", "intern_sync", "__bootstrap_import", "cursor", "parser",
        "resolver", "cartographer"
    };
    for (const char* name : names) {
        root->remove(name);
    }
}

StartupOptions parseStartupOptions(int argc, char** argv) {
    StartupOptions options;
    options.sessionBase = envOrDefault("EDICT_SESSION_BASE", "/tmp/session");

    int i = 1;
    while (i < argc) {
        const std::string arg = argv[i];
        auto requireValue = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--session" || arg == "--session-id") {
            options.sessionEnabled = true;
            options.sessionId = requireValue(arg.c_str());
            ++i;
        } else if (arg == "--session-base") {
            options.sessionBase = requireValue("--session-base");
            ++i;
        } else if (arg == "-h" || arg == "--help") {
            options.help = true;
            ++i;
        } else {
            break;
        }
    }

    options.commandIndex = i;
    return options;
}

StartupSession prepareSession(const StartupOptions& options,
                              CPtr<agentc::ListreeValue>& root) {
    StartupSession session;
    if (!options.sessionEnabled) {
        return session;
    }
    if (!isSafeSessionId(options.sessionId)) {
        throw std::runtime_error(
            "Invalid session id: use only letters, digits, '.', '-', and '_' and avoid '.' or '..'");
    }

    session.enabled = true;
    session.sessionId = options.sessionId;
    session.sessionBase = options.sessionBase;
    session.store = std::make_unique<agentc::runtime::SessionStateStore>(
        session.sessionBase, session.sessionId);

    if (session.store->exists()) {
        std::string error;
        if (!session.store->loadRoot(root, &error)) {
            throw std::runtime_error("Failed to restore Edict session '" + session.sessionId +
                                     "': " + error);
        }
        stripVolatileStartupBindings(root);
        session.restored = true;
    }

    if (!root) {
        root = agentc::createNullValue();
    }
    return session;
}

bool saveSession(const StartupSession& session, agentc::edict::EdictVM& vm) {
    if (!session.enabled) {
        return true;
    }
    auto root = vm.getCursor().getValue();
    if (!root) {
        std::cerr << "Error: cannot persist Edict session '" << session.sessionId
                  << "': VM root is null" << std::endl;
        return false;
    }
    stripVolatileStartupBindings(root);
    std::string error;
    if (!session.store->saveRoot(root, &error)) {
        std::cerr << "Error: failed to persist Edict session '" << session.sessionId
                  << "': " << error << std::endl;
        return false;
    }
    return true;
}

}

static void printUsage(const char* name) {
    std::cout << "Usage:\n";
    std::cout << "  " << name << " [--session ID] [--session-base DIR]            # start REPL\n";
    std::cout << "  " << name << " [--session ID] [--session-base DIR] -e CODE   # evaluate CODE and print stack\n";
    std::cout << "  " << name << " [--session ID] [--session-base DIR] FILE      # execute script file\n";
    std::cout << "  " << name << " [--session ID] [--session-base DIR] -         # execute script from stdin\n";
    std::cout << "  " << name << " [--session ID] [--session-base DIR] --ipc <IN> <OUT> # start IPC mode (named pipes)\n";
    std::cout << "  " << name << " [--session ID] [--session-base DIR] --socket <PATH> # start Socket mode (Unix Domain)\n";
    std::cout << "\nSession options:\n";
    std::cout << "  --session, --session-id ID  create/resume a named Edict session\n";
    std::cout << "  --session-base DIR         base directory for session state (default: EDICT_SESSION_BASE or /tmp/session)\n";
    std::cout << "\nSession ids may contain only letters, digits, '.', '-', and '_' and may not be '.' or '..'.\n";
}

static std::string joinArgs(int argc, char** argv, int start) {
    std::ostringstream oss;
    for (int i = start; i < argc; ++i) {
        if (i > start) oss << ' ';
        oss << argv[i];
    }
    return oss.str();
}

static size_t stackListCount(CPtr<agentc::ListreeValue> items) {
    size_t count = 0;
    if (!items) return 0;
    items->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) ++count;
    }, false);
    return count;
}

static CPtr<agentc::ListreeValue> stackListAt(CPtr<agentc::ListreeValue> items, size_t index) {
    if (!items) return nullptr;
    size_t current = 0;
    CPtr<agentc::ListreeValue> result = nullptr;
    items->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (result || !ref || !ref->getValue()) return;
        if (current == index) {
            result = ref->getValue();
            return;
        }
        ++current;
    }, false);
    return result;
}

int main(int argc, char** argv) {
    try {
        startupTrace("main-enter");
        StartupOptions options = parseStartupOptions(argc, argv);
        if (options.help) {
            printUsage(argv[0]);
            return 0;
        }

        // Create or restore a root node for the cursor.
        CPtr<agentc::ListreeValue> root;
        StartupSession session = prepareSession(options, root);
        const int commandIndex = options.commandIndex;

        if (commandIndex < argc) {
            std::string mode = argv[commandIndex];
            if (mode == "-e") {
                if (commandIndex + 1 >= argc) {
                    printUsage(argv[0]);
                    return 2;
                }
                currentDebugLevel = DEBUG_WARNING;
                std::string source = joinArgs(argc, argv, commandIndex + 1);
                agentc::edict::EdictCompiler compiler;
                agentc::edict::EdictVM vm(root);
                agentc::edict::BytecodeBuffer code = compiler.compile(source);
                int result = vm.execute(code);
                if (result & agentc::edict::VM_ERROR) {
                    std::cerr << "Error: " << vm.getError() << std::endl;
                    return 1;
                }
                auto items = vm.dumpStack();
                const size_t count = stackListCount(items);
                std::cout << "stack size: " << count << " (top first)" << std::endl;
                for (size_t i = 0; i < count; ++i) {
                    auto item = stackListAt(items, i);
                    std::string text = item && item->getData() ? std::string(static_cast<char*>(item->getData()), item->getLength()) : std::string();
                    std::cout << i << ": " << text << std::endl;
                }
                return saveSession(session, vm) ? 0 : 1;
            }

            // Script from stdin: edict -
            if (mode == "-") {
                currentDebugLevel = DEBUG_WARNING;
                startupTrace("stdin-script-before-repl");
                agentc::edict::EdictREPL repl(root, std::cin, std::cout);
                startupTrace("stdin-script-after-repl");
                const bool ok = repl.runScript(std::cin);
                if (ok && !saveSession(session, repl.getVM())) return 1;
                return ok ? 0 : 1;
            }

            // IPC Mode: edict --ipc <in> <out>
            if (mode == "--ipc") {
                if (commandIndex + 2 >= argc) {
                    printUsage(argv[0]);
                    return 2;
                }
                currentDebugLevel = DEBUG_WARNING;
                std::ifstream input(argv[commandIndex + 1]);
                std::ofstream output(argv[commandIndex + 2]);
                
                if (!input.is_open() || !output.is_open()) {
                    std::cerr << "Error: cannot open IPC files: " << argv[commandIndex + 1]
                              << ", " << argv[commandIndex + 2] << std::endl;
                    return 1;
                }
                
                std::cerr << "IPC mode: input/output open" << std::endl;
                output << std::unitbuf; // Force unbuffered output for live IPC
                
                agentc::edict::EdictREPL repl(root, input, output);
                repl.run();
                return saveSession(session, repl.getVM()) ? 0 : 1;
            }

            // Socket Mode: edict --socket <path>
            if (mode == "--socket") {
                if (commandIndex + 1 >= argc) {
                    printUsage(argv[0]);
                    return 2;
                }
                currentDebugLevel = DEBUG_WARNING;
                const char* socketPath = argv[commandIndex + 1];
                unlink(socketPath);
                
                int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (server_fd == -1) {
                    perror("socket");
                    return 1;
                }
                
                struct sockaddr_un addr;
                memset(&addr, 0, sizeof(addr));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);
                
                if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
                    perror("bind");
                    return 1;
                }
                
                if (listen(server_fd, 1) == -1) {
                    perror("listen");
                    return 1;
                }
                
                std::cerr << "Socket mode: listening on " << socketPath << std::endl;
                
                int client_fd = accept(server_fd, nullptr, nullptr);
                if (client_fd == -1) {
                    perror("accept");
                    return 1;
                }
                
                // Force unbuffered I/O for the socket client
                std::cout << std::unitbuf;
                
                // Redirect stdin/stdout to the socket
                dup2(client_fd, STDIN_FILENO);
                dup2(client_fd, STDOUT_FILENO);
                
                agentc::edict::EdictREPL repl(root, std::cin, std::cout);
                std::cout << "VM-READY" << std::endl; // Marker for the client
                repl.run();
                const bool saved = saveSession(session, repl.getVM());
                
                close(client_fd);
                close(server_fd);
                unlink(socketPath);
                return saved ? 0 : 1;
            }

            // Script from file: edict FILE
            if (mode[0] != '-') {
                currentDebugLevel = DEBUG_WARNING;
                std::ifstream file(mode);
                if (!file.is_open()) {
                    std::cerr << "Error: cannot open file: " << mode << std::endl;
                    return 1;
                }
                agentc::edict::EdictREPL repl(root, file, std::cout);
                const bool ok = repl.runScript(file);
                if (ok && !saveSession(session, repl.getVM())) return 1;
                return ok ? 0 : 1;
            }

            printUsage(argv[0]);
            return 2;
        }

        // Create and run the REPL
        startupTrace("interactive-before-repl");
        agentc::edict::EdictREPL repl(root, std::cin, std::cout);
        startupTrace("interactive-after-repl");
        repl.run();
        return saveSession(session, repl.getVM()) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
