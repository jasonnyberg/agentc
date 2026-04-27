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

// No constructor implementation here, inline in header

void EdictREPL::run() {
    output << "Edict REPL v0.1" << std::endl;
    output << "Type 'exit' to quit, 'help' for commands" << std::endl;
    
    // In IPC mode, we should not print the prompt if it's not a TTY
    // But EdictREPL doesn't know if it's a TTY.
    // For now, let's just make it not print "> " if it's not a REPL
    
    bool isREPL = (&input == &std::cin);
    
    while (true) {
        if (isREPL) output << "> ";
        std::string line = readLine();
        
        // Handle natural EOF exit first
        if (input.eof()) {
            output << std::endl;
            break; 
        }

        if (line == "exit" || line == "quit") {
            break;
        } else if (handleSpecialCommand(line)) {
            continue;
        } else {
            processLine(line);
        }
    }
}

std::string EdictREPL::readLine() {
    std::string line;
    std::getline(input, line);
    
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
    output.flush();
}

void EdictREPL::printResult(const Value& result) {
    output << "=> " << result.toString() << std::endl;
}

void EdictREPL::printHelp() {
    output << "Edict REPL Commands:" << std::endl;
    output << "  help       - Show this help message" << std::endl;
    output << "  exit, quit - Exit the REPL" << std::endl;
    output << "  clear      - Clear the VM state" << std::endl;
    output << "  stack      - Show the current stack contents" << std::endl;
    output << std::endl;
    output << "Edict Language:" << std::endl;
    output << "  [...]      - Literal (pushes code/string onto the stack)" << std::endl;
    output << "  (...)      - Literal (synonym for [...])" << std::endl;
    output << "  !          - Evaluate (executes code from the stack)" << std::endl;
    output << "  @          - Assign (assigns a value to a name)" << std::endl;
    output << "  $          - Reference (gets a value by name)" << std::endl;
    output << "  /          - Remove (removes a name from the dictionary)" << std::endl;
    output << "  {}         - Context (creates a new dictionary context)" << std::endl;
    output << std::endl;
}

void EdictREPL::printError(const std::string& error) {
    output << "Error: " << error << std::endl;
}

bool EdictREPL::runScript(std::istream& in) {
    std::string line;
    int lineNum = 0;
    while (std::getline(in, line)) {
        ++lineNum;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        try {
            BytecodeBuffer code = compiler.compile(line);
            int result = vm.execute(code);
            if (result & VM_ERROR) {
                output << "Error (line " << lineNum << "): " << vm.getError() << std::endl;
                return false;
            }
        } catch (const std::exception& e) {
            output << "Error (line " << lineNum << "): " << e.what() << std::endl;
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
        output << "VM state cleared" << std::endl;
        return true;
    } else if (line == "stack") {
        output << "Stack contents:" << std::endl;
        auto items = vm.dumpStack();
        const size_t count = stackListCount(items);
        for (size_t i = 0; i < count; i++) {
            auto item = stackListAt(items, i);
            std::string text = item && item->getData() ? std::string(static_cast<char*>(item->getData()), item->getLength()) : std::string();
            output << "  " << i << ": " << text << std::endl;
        }
        return true;
    }
    
    return false;
}

} // namespace agentc::edict
