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

#include <istream>
#include <string>
#include <vector>
#include "../core/cursor.h"
#include "edict_types.h"
#include "edict_compiler.h"
#include "edict_vm.h"

namespace agentc::edict {

// Forward declarations
class EdictCompiler;

// REPL class
class EdictREPL {
public:
    EdictREPL(CPtr<agentc::ListreeValue> root) : vm(root), compiler() {
        // Register cursor operations
        vm.registerCursorOperations();
    }
    
    void run();

    // Execute edict source from a stream line by line (script/pipe mode).
    // Lines beginning with '#' and blank lines are skipped.
    // Returns true on success, false if any line produces a VM error.
    bool runScript(std::istream& in);

private:
    EdictVM vm;
    EdictCompiler compiler;
    
    // Input/output
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
};

} // namespace agentc::edict
