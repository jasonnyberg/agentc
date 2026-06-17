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

#pragma once

#include <functional>
#include <istream>
#include <string>
#include <vector>
#include "../core/cursor.h"
#include "edict_types.h"
#include "edict_compiler.h"
#include "edict_vm.h"
#include "block_accumulator.h"

namespace agentc::edict {

// Forward declarations
class EdictCompiler;

// REPL class
class EdictREPL {
public:
    EdictREPL(CPtr<agentc::ListreeValue> root,
              std::vector<CPtr<agentc::ListreeValue>> staticBases = {})
        : vm(root, std::move(staticBases)), compiler(), input(std::cin), output(std::cout), historyIndex(0) {
        vm.registerCursorOperations();
    }

    EdictREPL(CPtr<agentc::ListreeValue> root, std::istream& in, std::ostream& out,
              std::vector<CPtr<agentc::ListreeValue>> staticBases = {})
        : vm(root, std::move(staticBases)), compiler(), input(in), output(out), historyIndex(0) {
        vm.registerCursorOperations();
    }
    
    void run();
    EdictVM& getVM() { return vm; }

    // Execute edict source from a stream line by line (script/pipe mode).
    // Lines beginning with '#' and blank lines are skipped.
    // Returns true on success, false if any line produces a VM error.
    bool runScript(std::istream& in);

    // Optional callback invoked between each REPL turn.  Used to pump the
    // Root1 await scheduler so yielded continuations can receive mailbox
    // descriptors without waiting for the next user input.
    void setSchedulerPump(std::function<void()> pump) { schedulerPump_ = std::move(pump); }

private:
    EdictVM vm;
    EdictCompiler compiler;
    
    // Input/output
    std::istream& input;
    std::ostream& output;

    std::string readLine();
    void processLine(const std::string& line);
    void printResult(const Value& result);
    void printHelp();
    void printError(const std::string& error);
    
    // Command history
    std::vector<std::string> history;
    size_t historyIndex = 0;
    
    // Special commands
    bool handleSpecialCommand(const std::string& line);

    // Optional scheduler pump
    std::function<void()> schedulerPump_;
};

} // namespace agentc::edict