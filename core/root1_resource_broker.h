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

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentc::root1 {

using ParticipantId = uint64_t;
using GrantToken = uint64_t;

struct ResourceKey {
    uint32_t layerId = 0;
    uint32_t slabId = 0;
    uint32_t offset = 0;
    uint16_t allocatorKind = 0;
    uint16_t fieldId = 0;
    uint64_t generation = 0;

    bool operator==(const ResourceKey& other) const;
};

struct ResourceKeyHash {
    size_t operator()(const ResourceKey& key) const;
};

class ResourceState {
public:
    ResourceState() = default;
    ResourceState(const ResourceState&) = delete;
    ResourceState& operator=(const ResourceState&) = delete;

    uint64_t raw() const;
    ParticipantId owner() const;
    bool isOwned() const;
    bool isContended() const;

private:
    friend class Root1ResourceBroker;

    static constexpr uint64_t kContendedBit = 1ull << 63;
    static constexpr uint64_t kOwnerMask = ~kContendedBit;

    std::atomic<uint64_t> word_{0};
};

enum class AcquireStatus {
    Acquired,
    Queued,
    InvalidParticipant,
};

enum class BrokerEventKind {
    OwnershipGranted,
    MailboxMessage,
};

struct BrokerEvent {
    BrokerEventKind kind = BrokerEventKind::MailboxMessage;
    ResourceKey resource;
    GrantToken grantToken = 0;
    uint64_t sequence = 0;
    std::string payload;
};

class Root1ResourceBroker {
public:
    Root1ResourceBroker();
    ~Root1ResourceBroker();

    Root1ResourceBroker(const Root1ResourceBroker&) = delete;
    Root1ResourceBroker& operator=(const Root1ResourceBroker&) = delete;

    bool available() const;

    ParticipantId registerParticipant();
    bool hasParticipant(ParticipantId participant) const;
    int participantEventFd(ParticipantId participant) const;

    bool tryAcquire(ResourceState& state, ParticipantId participant) const;
    AcquireStatus acquireOrQueue(const ResourceKey& key, ResourceState& state, ParticipantId participant);
    bool release(const ResourceKey& key, ResourceState& state, ParticipantId owner);

    bool sendMailboxMessage(ParticipantId participant, std::string payload, uint64_t sequence = 0);
    std::vector<ParticipantId> pollReadyParticipants(int timeoutMs, size_t maxEvents = 16);
    std::vector<BrokerEvent> drainMailbox(ParticipantId participant);

private:
    struct ParticipantRecord {
        int eventFd = -1;
        std::vector<BrokerEvent> mailbox;
    };

    bool isValidParticipantLocked(ParticipantId participant) const;
    bool notifyParticipantLocked(ParticipantId participant) const;
    void pushEventLocked(ParticipantId participant, BrokerEvent event);
    bool tryAcquireUnlocked(ResourceState& state, ParticipantId participant) const;
    void closeAllFds();

    mutable std::mutex mutex_;
    int epollFd_ = -1;
    ParticipantId nextParticipant_ = 1;
    GrantToken nextGrantToken_ = 1;
    std::unordered_map<ParticipantId, ParticipantRecord> participants_;
    std::unordered_map<ResourceKey, std::deque<ParticipantId>, ResourceKeyHash> waiters_;
};

} // namespace agentc::root1
