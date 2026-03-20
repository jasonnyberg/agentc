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

#include "edict_repl.h"
#include <iostream>

namespace agentc::edict {

namespace {

size_t stackListCount(CPtr<agentc::ListreeValue> items) {
    size_t count = 0;
    if (!items) return 0;
    items->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) ++count;
    }, false);
    return count;
}

CPtr<agentc::ListreeValue> stackListAt(CPtr<agentc::ListreeValue> items, size_t index) {
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

} // namespace

void EdictREPL::run() {
    std::cout << "Edict REPL v0.1" << std::endl;
    std::cout << "Type 'exit' to quit, 'help' for commands" << std::endl;
    
    while (true) {
        std::cout << "> ";
        std::string line = readLine();
        
        if (std::cin.eof()) {
            std::cout << std::endl; // Add a newline for clean exit
            break;
        }

        if (line == "exit" || line == "quit") {
            break;
        } else if (handleSpecialCommand(line)) {
            // Special command was handled
            continue;
        } else {
            processLine(line);
        }
    }
}

std::string EdictREPL::readLine() {
    std::string line;
    std::getline(std::cin, line);
    
    // Add to history if not empty and not a duplicate of the last entry
    if (!line.empty() && (history.empty() || history.back() != line)) {
        history.push_back(line);
        historyIndex = history.size();
    }
    
    return line;
}

void EdictREPL::processLine(const std::string& line) {
    try {
        BytecodeBuffer code = compiler.compile(line);
        int result = vm.execute(code);
        
        if (result & VM_ERROR) {
            printError(vm.getError());
        } else if (vm.getStackSize() != 0) {
            auto stackTop = vm.getStackTop();
            if (stackTop) {
                printResult(agentc::Cursor(stackTop).getName());
            } else {
                printResult("<null>");
            }
        }
    } catch (const std::exception& e) {
        printError(e.what());
    }
}

void EdictREPL::printResult(const Value& result) {
    std::cout << "=> " << result.toString() << std::endl;
}

void EdictREPL::printHelp() {
    std::cout << "Edict REPL Commands:" << std::endl;
    std::cout << "  help       - Show this help message" << std::endl;
    std::cout << "  exit, quit - Exit the REPL" << std::endl;
    std::cout << "  clear      - Clear the VM state" << std::endl;
    std::cout << "  stack      - Show the current stack contents" << std::endl;
    std::cout << std::endl;
    std::cout << "Edict Language:" << std::endl;
    std::cout << "  [...]      - Literal (pushes code/string onto the stack)" << std::endl;
    std::cout << "  (...)      - Literal (synonym for [...])" << std::endl;
    std::cout << "  !          - Evaluate (executes code from the stack)" << std::endl;
    std::cout << "  @          - Assign (assigns a value to a name)" << std::endl;
    std::cout << "  $          - Reference (gets a value by name)" << std::endl;
    std::cout << "  /          - Remove (removes a name from the dictionary)" << std::endl;
    std::cout << "  {}         - Context (creates a new dictionary context)" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  42 [answer] @      - Assign 42 to \"answer\"" << std::endl;
    std::cout << "  [answer] $         - Get the value of \"answer\"" << std::endl;
    std::cout << "  [1 2 +] !          - Evaluate \"1 2 +\" (pushes 3)" << std::endl;
    std::cout << "  {} [x] @ [x] $ !   - Create context, assign to x, get x, evaluate" << std::endl;
}

void EdictREPL::printError(const std::string& error) {
    std::cout << "Error: " << error << std::endl;
}

bool EdictREPL::runScript(std::istream& in) {
    std::string line;
    int lineNum = 0;
    while (std::getline(in, line)) {
        ++lineNum;
        // Strip trailing carriage return (for Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Skip blank lines and comment lines
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        try {
            BytecodeBuffer code = compiler.compile(line);
            int result = vm.execute(code);
            if (result & VM_ERROR) {
                std::cerr << "Error (line " << lineNum << "): " << vm.getError() << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error (line " << lineNum << "): " << e.what() << std::endl;
            return false;
        }
    }
    return true;
}

bool EdictREPL::handleSpecialCommand(const std::string& line) {
    if (line == "help") {
        printHelp();
        return true;
    } else if (line == "clear") {
        vm.reset();
        std::cout << "VM state cleared" << std::endl;
        return true;
    } else if (line == "stack") {
        std::cout << "Stack contents:" << std::endl;
        auto items = vm.dumpStack();
        const size_t count = stackListCount(items);
        for (size_t i = 0; i < count; i++) {
            auto item = stackListAt(items, i);
            std::string text = item && item->getData() ? std::string(static_cast<char*>(item->getData()), item->getLength()) : std::string();
            std::cout << "  " << i << ": " << text << std::endl;
        }
        return true;
    }
    
    return false;
}

} // namespace agentc::edict
