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

#include <algorithm>
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

Root1ContinuationHandle Root1AwaitScheduler::park(agentc::root1::ParticipantId participant,
                                                  ResumeCallback resume) {
    if (participant == 0) {
        return 0;
    }
    const auto handle = nextId_++;
    continuations_.emplace(handle, ParkedContinuation{
        handle,
        participant,
        Root1ContinuationState::Parked,
        0,
        0,
        nullptr,
        std::move(resume),
    });
    byParticipant_[participant].push_back(handle);
    return handle;
}

Root1ContinuationHandle Root1AwaitScheduler::parkVm(agentc::root1::ParticipantId participant,
                                                    EdictVM& vm) {
    return park(participant, [&vm](CPtr<agentc::ListreeValue> eventsValue) {
        vm.pushData(eventsValue);
        return vm.resume();
    });
}

Root1ContinuationStatus Root1AwaitScheduler::status(Root1ContinuationHandle handle) const {
    auto it = continuations_.find(handle);
    if (it == continuations_.end()) {
        return Root1ContinuationStatus{};
    }
    const auto& continuation = it->second;
    return Root1ContinuationStatus{
        continuation.handle,
        continuation.participant,
        continuation.state,
        continuation.eventsDelivered,
        continuation.resumeState,
    };
}

CPtr<agentc::ListreeValue> Root1AwaitScheduler::events(Root1ContinuationHandle handle) const {
    auto it = continuations_.find(handle);
    if (it == continuations_.end()) {
        return nullptr;
    }
    return it->second.lastEvents;
}

bool Root1AwaitScheduler::cancel(Root1ContinuationHandle handle) {
    return markTerminal(handle, Root1ContinuationState::Cancelled);
}

bool Root1AwaitScheduler::drop(Root1ContinuationHandle handle) {
    auto it = continuations_.find(handle);
    if (it == continuations_.end()) {
        return false;
    }
    removeFromParticipantIndex(handle, it->second.participant);
    continuations_.erase(it);
    return true;
}

size_t Root1AwaitScheduler::parkedCount() const {
    size_t count = 0;
    for (const auto& entry : continuations_) {
        if (entry.second.state == Root1ContinuationState::Parked) {
            ++count;
        }
    }
    return count;
}

Root1AwaitPollResult Root1AwaitScheduler::pollAndResume(agentc::root1::Root1ResourceBroker& broker,
                                                        int timeoutMs) {
    Root1AwaitPollResult result;
    result.readyParticipants = broker.pollReadyParticipants(timeoutMs);

    if (result.readyParticipants.empty() && timeoutMs > 0) {
        std::vector<Root1ContinuationHandle> timedOut;
        for (const auto& entry : continuations_) {
            if (entry.second.state == Root1ContinuationState::Parked) {
                timedOut.push_back(entry.first);
            }
        }
        for (const auto handle : timedOut) {
            if (markTerminal(handle, Root1ContinuationState::Timeout)) {
                ++result.timedOutContinuations;
            }
        }
        return result;
    }

    for (const auto participant : result.readyParticipants) {
        auto it = byParticipant_.find(participant);
        if (it == byParticipant_.end() || it->second.empty()) {
            continue;
        }

        auto descriptors = broker.drainMailboxDescriptors(participant);
        if (descriptors.empty()) {
            continue;
        }

        auto handles = std::move(it->second);
        byParticipant_.erase(it);
        for (const auto handle : handles) {
            auto continuationIt = continuations_.find(handle);
            if (continuationIt == continuations_.end() ||
                continuationIt->second.state != Root1ContinuationState::Parked) {
                continue;
            }

            auto& continuation = continuationIt->second;
            continuation.lastEvents = descriptorsToList(descriptors);
            continuation.eventsDelivered = eventCount(continuation.lastEvents);
            if (continuation.resume) {
                continuation.resumeState = continuation.resume(continuation.lastEvents);
                continuation.state = Root1ContinuationState::Resumed;
                ++result.resumedContinuations;
            } else {
                continuation.state = Root1ContinuationState::Ready;
                ++result.readyContinuations;
            }
        }
    }

    return result;
}

void Root1AwaitScheduler::removeFromParticipantIndex(Root1ContinuationHandle handle,
                                                     agentc::root1::ParticipantId participant) {
    auto it = byParticipant_.find(participant);
    if (it == byParticipant_.end()) {
        return;
    }
    auto& handles = it->second;
    handles.erase(std::remove(handles.begin(), handles.end(), handle), handles.end());
    if (handles.empty()) {
        byParticipant_.erase(it);
    }
}

bool Root1AwaitScheduler::markTerminal(Root1ContinuationHandle handle,
                                       Root1ContinuationState state) {
    auto it = continuations_.find(handle);
    if (it == continuations_.end() || it->second.state != Root1ContinuationState::Parked) {
        return false;
    }
    removeFromParticipantIndex(handle, it->second.participant);
    it->second.state = state;
    return true;
}

size_t Root1AwaitScheduler::eventCount(const CPtr<agentc::ListreeValue>& eventsValue) const {
    if (!eventsValue || !eventsValue->isListMode()) {
        return 0;
    }
    size_t count = 0;
    eventsValue->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            ++count;
        }
    });
    return count;
}

CPtr<agentc::ListreeValue> Root1AwaitScheduler::saveState() const {
    auto state = agentc::createNullValue();
    agentc::addNamedItem(state, "next_id", agentc::createStringValue(std::to_string(nextId_)));
    auto contList = agentc::createListValue();
    for (const auto& entry : continuations_) {
        const auto& c = entry.second;
        if (c.state == Root1ContinuationState::Invalid) continue;
        auto cv = agentc::createNullValue();
        agentc::addNamedItem(cv, "handle", agentc::createStringValue(std::to_string(c.handle)));
        agentc::addNamedItem(cv, "participant", agentc::createStringValue(std::to_string(c.participant)));
        std::string stateName;
        switch (c.state) {
            case Root1ContinuationState::Parked: stateName = "parked"; break;
            case Root1ContinuationState::Ready: stateName = "ready"; break;
            case Root1ContinuationState::Resumed: stateName = "resumed"; break;
            case Root1ContinuationState::Timeout: stateName = "timeout"; break;
            case Root1ContinuationState::Cancelled: stateName = "cancelled"; break;
            default: stateName = "invalid"; break;
        }
        agentc::addNamedItem(cv, "state", agentc::createStringValue(stateName));
        agentc::addNamedItem(cv, "events_delivered", agentc::createStringValue(std::to_string(c.eventsDelivered)));
        agentc::addNamedItem(cv, "resume_state", agentc::createStringValue(std::to_string(c.resumeState)));
        agentc::addListItem(contList, cv);
    }
    agentc::addNamedItem(state, "continuations", contList);
    return state;
}

bool Root1AwaitScheduler::loadState(CPtr<agentc::ListreeValue> state) {
    if (!state) return false;
    auto nextIdItem = state->find("next_id");
    if (!nextIdItem) return false;
    auto nextIdVal = nextIdItem->getValue();
    if (!nextIdVal || !nextIdVal->getData()) return false;
    nextId_ = std::stoull(std::string((char*)nextIdVal->getData(), nextIdVal->getLength()));

    continuations_.clear();
    byParticipant_.clear();

    auto contListItem = state->find("continuations");
    if (!contListItem) return true;  // empty state is valid
    auto contList = contListItem->getValue();
    if (!contList || !contList->isListMode()) return true;

    contList->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (!ref) return;
        auto cv = ref->getValue();
        if (!cv) return;
        auto handleItem = cv->find("handle");
        if (!handleItem) return;
        auto handleVal = handleItem->getValue();
        if (!handleVal || !handleVal->getData()) return;
        auto handle = std::stoull(std::string((char*)handleVal->getData(), handleVal->getLength()));
        auto participantItem = cv->find("participant");
        if (!participantItem) return;
        auto participant = std::stoull(std::string((char*)participantItem->getValue()->getData(),
                                                    participantItem->getValue()->getLength()));
        auto stateItem = cv->find("state");
        std::string stateStr;
        if (stateItem && stateItem->getValue() && stateItem->getValue()->getData()) {
            stateStr = std::string((char*)stateItem->getValue()->getData(),
                                    stateItem->getValue()->getLength());
        }
        Root1ContinuationState contState = Root1ContinuationState::Parked;
        if (stateStr == "ready") contState = Root1ContinuationState::Ready;
        else if (stateStr == "resumed") contState = Root1ContinuationState::Resumed;
        else if (stateStr == "timeout") contState = Root1ContinuationState::Timeout;
        else if (stateStr == "cancelled") contState = Root1ContinuationState::Cancelled;

        size_t eventsDelivered = 0;
        auto edItem = cv->find("events_delivered");
        if (edItem && edItem->getValue() && edItem->getValue()->getData()) {
            eventsDelivered = std::stoull(std::string((char*)edItem->getValue()->getData(),
                                                       edItem->getValue()->getLength()));
        }
        int resumeState = 0;
        auto rsItem = cv->find("resume_state");
        if (rsItem && rsItem->getValue() && rsItem->getValue()->getData()) {
            resumeState = std::stoi(std::string((char*)rsItem->getValue()->getData(),
                                                  rsItem->getValue()->getLength()));
        }
        continuations_.emplace(handle, ParkedContinuation{
            handle, static_cast<agentc::root1::ParticipantId>(participant),
            contState, eventsDelivered, resumeState, nullptr, ResumeCallback{}
        });
        byParticipant_[static_cast<agentc::root1::ParticipantId>(participant)].push_back(handle);
    });
    return true;
}

} // namespace agentc::edict
