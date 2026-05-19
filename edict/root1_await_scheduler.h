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
#include "../listree/listree.h"

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <vector>

namespace agentc::edict {

class EdictVM;

using Root1ContinuationHandle = uint64_t;

enum class Root1ContinuationState {
    Invalid,
    Parked,
    Ready,
    Resumed,
    Timeout,
    Cancelled,
};

struct Root1ContinuationStatus {
    Root1ContinuationHandle handle = 0;
    agentc::root1::ParticipantId participant = 0;
    Root1ContinuationState state = Root1ContinuationState::Invalid;
    size_t eventsDelivered = 0;
    int resumeState = 0;
};

struct Root1AwaitPollResult {
    std::vector<agentc::root1::ParticipantId> readyParticipants;
    size_t readyContinuations = 0;
    size_t resumedContinuations = 0;
    size_t timedOutContinuations = 0;
};

// First scheduler-side await table for G110.  The public surface is logical:
// callers receive continuation handles and can query parked/ready/resumed/
// timeout/cancelled status.  The scheduler keeps Root1's fd/epoll state in the
// broker and parks on logical participant waitables.  A test-only VM adapter is
// provided through parkVm(...), but the parking record stores a logical handle
// plus an optional resume callback rather than exposing/storing raw EdictVM*.
class Root1AwaitScheduler {
public:
    using ResumeCallback = std::function<int(CPtr<agentc::ListreeValue>)>;

    Root1ContinuationHandle park(agentc::root1::ParticipantId participant,
                                 ResumeCallback resume = {});
    Root1ContinuationHandle parkVm(agentc::root1::ParticipantId participant,
                                   EdictVM& vm);

    Root1ContinuationStatus status(Root1ContinuationHandle handle) const;
    CPtr<agentc::ListreeValue> events(Root1ContinuationHandle handle) const;
    bool cancel(Root1ContinuationHandle handle);
    bool drop(Root1ContinuationHandle handle);

    size_t parkedCount() const;
    Root1AwaitPollResult pollAndResume(agentc::root1::Root1ResourceBroker& broker,
                                       int timeoutMs);

private:
    struct ParkedContinuation {
        Root1ContinuationHandle handle = 0;
        agentc::root1::ParticipantId participant = 0;
        Root1ContinuationState state = Root1ContinuationState::Invalid;
        size_t eventsDelivered = 0;
        int resumeState = 0;
        CPtr<agentc::ListreeValue> lastEvents;
        ResumeCallback resume;
    };

    void removeFromParticipantIndex(Root1ContinuationHandle handle,
                                    agentc::root1::ParticipantId participant);
    bool markTerminal(Root1ContinuationHandle handle, Root1ContinuationState state);
    size_t eventCount(const CPtr<agentc::ListreeValue>& events) const;

    Root1ContinuationHandle nextId_ = 1;
    std::unordered_map<Root1ContinuationHandle, ParkedContinuation> continuations_;
    std::unordered_map<agentc::root1::ParticipantId, std::vector<Root1ContinuationHandle>> byParticipant_;
};

} // namespace agentc::edict
