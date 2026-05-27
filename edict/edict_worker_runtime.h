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

#include "../listree/listree.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace agentc::edict::worker {

struct InternWorkerInput {
    std::string taskId;
    std::string program;
    CPtr<agentc::ListreeValue> inputSnapshot;
    CPtr<agentc::ListreeValue> contextSharedReadOnly;
    CPtr<agentc::ListreeValue> importsSharedReadOnly;
    CPtr<agentc::ListreeValue> staticMountsReadOnly;
    std::string staticProgramMount;
    std::string staticProgramWord;
    bool hasStaticProgramBytecode = false;
    std::vector<uint8_t> staticProgramBytecode;
    bool allowUnsafeFfiCalls = false;
    bool runInChildProcess = false;
    bool runWithExec = false;
    std::string workerExecPath;
    std::shared_ptr<std::atomic<bool>> cancelRequested;
    bool hasMaxActiveJobs = false;
    size_t maxActiveJobs = 0;
};

struct InternWorkerOutcome {
    bool ok = false;
    int vmState = 0;
    std::string resultJson = "null";
    std::string errorCode;
    std::string errorMessage;
};

struct InternForkedWorkerHandle {
    int childPid = -1;
    int readFd = -1;
};

class InternJoinSlot {
public:
    void store(InternWorkerOutcome value);
    InternWorkerOutcome load() const;
    bool ready() const;

private:
    mutable std::mutex mutex_;
    InternWorkerOutcome outcome_;
    bool ready_ = false;
};

void runInternWorker(InternWorkerInput input, InternJoinSlot& slot);
bool launchInternWorkerForked(InternWorkerInput input,
                              InternForkedWorkerHandle& handle,
                              std::string* launchError = nullptr);
bool collectInternWorkerForked(InternForkedWorkerHandle handle,
                               InternJoinSlot& slot,
                               std::string* launchError = nullptr);
bool runInternWorkerForked(InternWorkerInput input,
                           InternJoinSlot& slot,
                           std::string* launchError = nullptr,
                           int* childPid = nullptr);
bool launchInternWorkerExeced(const std::string& executablePath,
                              InternWorkerInput input,
                              InternForkedWorkerHandle& handle,
                              std::string* launchError = nullptr);
bool collectInternWorkerExeced(InternForkedWorkerHandle handle,
                               InternJoinSlot& slot,
                               std::string* launchError = nullptr);
bool runInternWorkerExeced(const std::string& executablePath,
                           InternWorkerInput input,
                           InternJoinSlot& slot,
                           std::string* launchError = nullptr,
                           int* childPid = nullptr);
int runInternWorkerExecChildMain(int argc, char** argv);

} // namespace agentc::edict::worker
