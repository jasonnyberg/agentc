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

#include "../core/root1_resource_broker.h"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace agentc::edict {

class EdictVM;

struct Root1AwaitPollResult {
    std::vector<agentc::root1::ParticipantId> readyParticipants;
    size_t resumedContinuations = 0;
};

// First small scheduler-side await table for G110.  This deliberately keeps
// Root1's fd/epoll state in the broker and keeps Edict-visible state as
// logical participant waitables.  Parked VMs must already be yielded; when a
// participant mailbox has descriptors, the scheduler pushes those descriptor
// values onto the VM data stack and resumes the existing code frame.
class Root1AwaitScheduler {
public:
    size_t park(agentc::root1::ParticipantId participant, EdictVM& vm);
    size_t parkedCount() const;
    Root1AwaitPollResult pollAndResume(agentc::root1::Root1ResourceBroker& broker,
                                       int timeoutMs);

private:
    struct ParkedContinuation {
        size_t id = 0;
        agentc::root1::ParticipantId participant = 0;
        EdictVM* vm = nullptr;
    };

    size_t nextId_ = 1;
    std::unordered_map<agentc::root1::ParticipantId, std::vector<ParkedContinuation>> parked_;
};

} // namespace agentc::edict
