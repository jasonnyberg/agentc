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

#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "edict_repl.h"
#include "edict_compiler.h"
#include "edict_vm.h"
#include "../core/alloc.h"
#include "../core/debug.h"

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

}

static void printUsage(const char* name) {
    std::cout << "Usage:\n";
    std::cout << "  " << name << "            # start REPL\n";
    std::cout << "  " << name << " -e CODE   # evaluate CODE and print stack\n";
    std::cout << "  " << name << " FILE      # execute script file\n";
    std::cout << "  " << name << " -         # execute script from stdin\n";
    std::cout << "  " << name << " --ipc <IN> <OUT> # start IPC mode (named pipes)\n";
    std::cout << "  " << name << " --socket <PATH> # start Socket mode (Unix Domain)\n";
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
        // Create a root node for the cursor
        CPtr<agentc::ListreeValue> root;

        if (argc > 1) {
            std::string mode = argv[1];
            if (mode == "-e") {
                if (argc < 3) {
                    printUsage(argv[0]);
                    return 2;
                }
                currentDebugLevel = DEBUG_WARNING;
                std::string source = joinArgs(argc, argv, 2);
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
                return 0;
            }

            // Script from stdin: edict -
            if (mode == "-") {
                currentDebugLevel = DEBUG_WARNING;
                startupTrace("stdin-script-before-repl");
                agentc::edict::EdictREPL repl(root, std::cin, std::cout);
                startupTrace("stdin-script-after-repl");
                return repl.runScript(std::cin) ? 0 : 1;
            }

            // IPC Mode: edict --ipc <in> <out>
            if (mode == "--ipc") {
                if (argc < 4) {
                    printUsage(argv[0]);
                    return 2;
                }
                currentDebugLevel = DEBUG_WARNING;
                std::ifstream input(argv[2]);
                std::ofstream output(argv[3]);
                
                if (!input.is_open() || !output.is_open()) {
                    std::cerr << "Error: cannot open IPC files: " << argv[2] << ", " << argv[3] << std::endl;
                    return 1;
                }
                
                std::cerr << "IPC mode: input/output open" << std::endl;
                output << std::unitbuf; // Force unbuffered output for live IPC
                
                agentc::edict::EdictREPL repl(root, input, output);
                repl.run();
                return 0;
            }

            // Socket Mode: edict --socket <path>
            if (mode == "--socket") {
                if (argc < 3) {
                    printUsage(argv[0]);
                    return 2;
                }
                currentDebugLevel = DEBUG_WARNING;
                const char* socketPath = argv[2];
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
                
                close(client_fd);
                close(server_fd);
                unlink(socketPath);
                return 0;
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
                return repl.runScript(file) ? 0 : 1;
            }

            printUsage(argv[0]);
            return 2;
        }

        // Create and run the REPL
        startupTrace("interactive-before-repl");
        agentc::edict::EdictREPL repl(root, std::cin, std::cout);
        startupTrace("interactive-after-repl");
        repl.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
