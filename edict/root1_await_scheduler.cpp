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

#include "root1_await_scheduler.h"
#include "edict_vm.h"

#include <string>

namespace agentc::edict {
namespace {

std::string descriptorEventName(agentc::root1::MailboxEventKind kind) {
    using agentc::root1::MailboxEventKind;
    switch (kind) {
        case MailboxEventKind::None: return "none";
        case MailboxEventKind::Message: return "message";
        case MailboxEventKind::OwnershipGranted: return "ownership_granted";
        case MailboxEventKind::Progress: return "progress";
        case MailboxEventKind::Complete: return "complete";
        case MailboxEventKind::Error: return "error";
        case MailboxEventKind::Cancelled: return "cancelled";
        case MailboxEventKind::Backpressure: return "backpressure";
        case MailboxEventKind::OwnerDied: return "owner_died";
    }
    return "unknown";
}

CPtr<agentc::ListreeValue> descriptorToValue(const agentc::root1::MailboxDescriptor& descriptor) {
    auto value = agentc::createNullValue();
    agentc::addNamedItem(value, "kind", agentc::createStringValue(descriptorEventName(descriptor.eventKind)));
    agentc::addNamedItem(value, "sequence", agentc::createStringValue(std::to_string(descriptor.sequence)));
    agentc::addNamedItem(value, "correlation_id", agentc::createStringValue(std::to_string(descriptor.correlationId)));
    agentc::addNamedItem(value, "grant_token", agentc::createStringValue(std::to_string(descriptor.grantToken)));
    const auto payload = agentc::root1::inlinePayload(descriptor);
    if (!payload.empty()) {
        agentc::addNamedItem(value, "payload", agentc::createStringValue(std::string(payload)));
    } else {
        agentc::addNamedItem(value, "payload", agentc::createNullValue());
    }
    return value;
}

CPtr<agentc::ListreeValue> descriptorsToList(const std::vector<agentc::root1::MailboxDescriptor>& descriptors) {
    auto list = agentc::createListValue();
    for (const auto& descriptor : descriptors) {
        agentc::addListItem(list, descriptorToValue(descriptor));
    }
    return list;
}

} // namespace

size_t Root1AwaitScheduler::park(agentc::root1::ParticipantId participant, EdictVM& vm) {
    if (participant == 0) {
        return 0;
    }
    const size_t id = nextId_++;
    parked_[participant].push_back(ParkedContinuation{id, participant, &vm});
    return id;
}

size_t Root1AwaitScheduler::parkedCount() const {
    size_t count = 0;
    for (const auto& entry : parked_) {
        count += entry.second.size();
    }
    return count;
}

Root1AwaitPollResult Root1AwaitScheduler::pollAndResume(agentc::root1::Root1ResourceBroker& broker,
                                                        int timeoutMs) {
    Root1AwaitPollResult result;
    result.readyParticipants = broker.pollReadyParticipants(timeoutMs);

    for (const auto participant : result.readyParticipants) {
        auto it = parked_.find(participant);
        if (it == parked_.end() || it->second.empty()) {
            continue;
        }

        auto descriptors = broker.drainMailboxDescriptors(participant);
        if (descriptors.empty()) {
            continue;
        }

        auto continuations = std::move(it->second);
        parked_.erase(it);
        for (auto& continuation : continuations) {
            if (!continuation.vm) {
                continue;
            }
            continuation.vm->pushData(descriptorsToList(descriptors));
            continuation.vm->resume();
            ++result.resumedContinuations;
        }
    }

    return result;
}

} // namespace agentc::edict
