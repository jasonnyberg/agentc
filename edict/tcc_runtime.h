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

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace agentc::edict::tcc {

struct TccEnvelope {
    bool ok = false;
    bool available = false;
    std::string status;
    std::string error;
    std::string moduleId;
    std::string jobId;
    std::string entrySymbol;
    std::string resultKind;
    std::string resultText;
    long long resultI64 = 0;
    double resultF64 = 0.0;
    int signalNumber = 0;
    int exitCode = 0;
    int pid = -1;
    long long timeoutMs = 0;
    std::size_t symbolCount = 0;
    std::string handleKind;
    std::string launchMode;
    std::vector<std::string> diagnostics;
    std::vector<std::string> symbols;
    std::vector<std::string> logs;
};

class TccCompilerService {
public:
    TccCompilerService();
    ~TccCompilerService();

    TccEnvelope availability() const;
    TccEnvelope compile(const std::string& source);
    TccEnvelope run(const std::string& moduleId, const std::vector<std::string>& args);
    TccEnvelope listSymbols(const std::string& moduleId) const;
    TccEnvelope drop(const std::string& moduleId);

    TccEnvelope startIsolated(const std::string& moduleId,
                              const std::vector<std::string>& args,
                              long long timeoutMs);
    TccEnvelope status(const std::string& jobId);
    TccEnvelope collect(const std::string& jobId);
    TccEnvelope cancel(const std::string& jobId);

    TccEnvelope allowProcessSymbol(const std::string& name,
                                   const std::string& declaration);
    TccEnvelope allowLibrarySymbol(const std::string& libraryPath,
                                   const std::string& name,
                                   const std::string& declaration);
    TccEnvelope clearAllowedSymbols();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

int runTccWorkerExecChildMain(int argc, char** argv);

} // namespace agentc::edict::tcc
