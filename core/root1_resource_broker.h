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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
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

enum class MailboxEventKind : uint16_t {
    None = 0,
    Message = 1,
    OwnershipGranted = 2,
    Progress = 3,
    Complete = 4,
    Error = 5,
    Cancelled = 6,
    Backpressure = 7,
};

enum class MailboxPayloadKind : uint16_t {
    None = 0,
    InlineBytes = 1,
    Json = 2,
    PublishedSlab = 3,
    BlobHandle = 4,
};

struct SlabPayloadHandle {
    uint32_t layerId = 0;
    uint32_t slabId = 0;
    uint32_t offset = 0;
    uint64_t generation = 0;
};

constexpr size_t kMailboxInlineBytes = 96;
constexpr size_t kMailboxRingCapacity = 64;

struct MailboxDescriptor {
    uint64_t sequence = 0;
    uint64_t correlationId = 0;
    GrantToken grantToken = 0;
    ResourceKey resource;
    SlabPayloadHandle payloadHandle;
    MailboxEventKind eventKind = MailboxEventKind::None;
    MailboxPayloadKind payloadKind = MailboxPayloadKind::None;
    uint32_t flags = 0;
    uint32_t inlineSize = 0;
    std::array<char, kMailboxInlineBytes> inlineBytes{};
};

bool setInlinePayload(MailboxDescriptor& descriptor, std::string_view payload);
std::string_view inlinePayload(const MailboxDescriptor& descriptor);

class MailboxRing {
public:
    bool tryPush(const MailboxDescriptor& descriptor);
    bool tryPop(MailboxDescriptor& descriptor);
    void resetForTests();
    size_t capacity() const;
    size_t approximateSize() const;
    bool empty() const;
    bool full() const;

private:
    std::atomic<uint64_t> head_{0};
    std::atomic<uint64_t> tail_{0};
    std::array<MailboxDescriptor, kMailboxRingCapacity> slots_{};
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
    bool sendMailboxDescriptor(ParticipantId participant, const MailboxDescriptor& descriptor);
    std::vector<ParticipantId> pollReadyParticipants(int timeoutMs, size_t maxEvents = 16);
    std::vector<BrokerEvent> drainMailbox(ParticipantId participant);
    std::vector<MailboxDescriptor> drainMailboxDescriptors(ParticipantId participant);

private:
    struct ParticipantRecord {
        int eventFd = -1;
        std::vector<BrokerEvent> mailbox;
        std::unique_ptr<MailboxRing> descriptorMailbox;
    };

    bool isValidParticipantLocked(ParticipantId participant) const;
    bool notifyParticipantLocked(ParticipantId participant) const;
    void pushEventLocked(ParticipantId participant, BrokerEvent event);
    bool pushDescriptorLocked(ParticipantId participant, const MailboxDescriptor& descriptor);
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
