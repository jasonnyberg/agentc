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

namespace agentc::edict::worker {

struct InternWorkerInput {
    std::string taskId;
    std::string program;
    CPtr<agentc::ListreeValue> inputSnapshot;
    CPtr<agentc::ListreeValue> contextSharedReadOnly;
    CPtr<agentc::ListreeValue> importsSharedReadOnly;
    bool allowUnsafeFfiCalls = false;
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

} // namespace agentc::edict::worker
