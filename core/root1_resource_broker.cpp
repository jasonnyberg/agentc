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

#include "root1_resource_broker.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

#if defined(__linux__)
#include <cerrno>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif

namespace agentc::root1 {

namespace {

size_t mixHash(size_t seed, uint64_t value) {
    seed ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

#if defined(__linux__)
bool drainEventFd(int fd) {
    bool drainedAny = false;
    while (true) {
        eventfd_t value = 0;
        int rc = eventfd_read(fd, &value);
        if (rc == 0) {
            drainedAny = true;
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return drainedAny;
        }
        return drainedAny;
    }
}
#endif

} // namespace

bool ResourceKey::operator==(const ResourceKey& other) const {
    return layerId == other.layerId &&
           slabId == other.slabId &&
           offset == other.offset &&
           allocatorKind == other.allocatorKind &&
           fieldId == other.fieldId &&
           generation == other.generation;
}

size_t ResourceKeyHash::operator()(const ResourceKey& key) const {
    size_t seed = 0;
    seed = mixHash(seed, key.layerId);
    seed = mixHash(seed, key.slabId);
    seed = mixHash(seed, key.offset);
    seed = mixHash(seed, key.allocatorKind);
    seed = mixHash(seed, key.fieldId);
    seed = mixHash(seed, key.generation);
    return seed;
}

bool setInlinePayload(MailboxDescriptor& descriptor, std::string_view payload) {
    if (payload.size() > descriptor.inlineBytes.size()) {
        return false;
    }
    descriptor.inlineBytes.fill('\0');
    if (!payload.empty()) {
        std::memcpy(descriptor.inlineBytes.data(), payload.data(), payload.size());
    }
    descriptor.inlineSize = static_cast<uint32_t>(payload.size());
    descriptor.payloadKind = MailboxPayloadKind::InlineBytes;
    return true;
}

std::string_view inlinePayload(const MailboxDescriptor& descriptor) {
    const size_t size = std::min<size_t>(descriptor.inlineSize, descriptor.inlineBytes.size());
    return std::string_view(descriptor.inlineBytes.data(), size);
}

bool MailboxRing::tryPush(const MailboxDescriptor& descriptor) {
    const uint64_t tail = tail_.load(std::memory_order_relaxed);
    const uint64_t head = head_.load(std::memory_order_acquire);
    if (tail - head >= slots_.size()) {
        return false;
    }

    slots_[tail % slots_.size()] = descriptor;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}

bool MailboxRing::tryPop(MailboxDescriptor& descriptor) {
    const uint64_t head = head_.load(std::memory_order_relaxed);
    const uint64_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) {
        return false;
    }

    descriptor = slots_[head % slots_.size()];
    head_.store(head + 1, std::memory_order_release);
    return true;
}

void MailboxRing::resetForTests() {
    head_.store(0, std::memory_order_release);
    tail_.store(0, std::memory_order_release);
    slots_.fill(MailboxDescriptor{});
}

size_t MailboxRing::capacity() const {
    return slots_.size();
}

size_t MailboxRing::approximateSize() const {
    const uint64_t head = head_.load(std::memory_order_acquire);
    const uint64_t tail = tail_.load(std::memory_order_acquire);
    return static_cast<size_t>(tail - head);
}

bool MailboxRing::empty() const {
    return approximateSize() == 0;
}

bool MailboxRing::full() const {
    return approximateSize() >= capacity();
}

uint64_t ResourceState::raw() const {
    return word_.load(std::memory_order_acquire);
}

ParticipantId ResourceState::owner() const {
    return raw() & kOwnerMask;
}

bool ResourceState::isOwned() const {
    return owner() != 0;
}

bool ResourceState::isContended() const {
    return (raw() & kContendedBit) != 0;
}

Root1ResourceBroker::Root1ResourceBroker() {
#if defined(__linux__)
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0) {
        throw std::runtime_error("Root1ResourceBroker: epoll_create1 failed");
    }
#else
    throw std::runtime_error("Root1ResourceBroker requires Linux eventfd/epoll");
#endif
}

Root1ResourceBroker::~Root1ResourceBroker() {
    closeAllFds();
}

bool Root1ResourceBroker::available() const {
    return epollFd_ >= 0;
}

ParticipantId Root1ResourceBroker::registerParticipant() {
#if defined(__linux__)
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("Root1ResourceBroker: eventfd failed");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ParticipantId id = nextParticipant_++;

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = id;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) != 0) {
        close(fd);
        throw std::runtime_error("Root1ResourceBroker: epoll_ctl add failed");
    }

    ParticipantRecord record;
    record.eventFd = fd;
    record.descriptorMailbox = std::make_unique<MailboxRing>();
    participants_.emplace(id, std::move(record));
    return id;
#else
    throw std::runtime_error("Root1ResourceBroker requires Linux eventfd/epoll");
#endif
}

bool Root1ResourceBroker::hasParticipant(ParticipantId participant) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return isValidParticipantLocked(participant);
}

int Root1ResourceBroker::participantEventFd(ParticipantId participant) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = participants_.find(participant);
    return it == participants_.end() ? -1 : it->second.eventFd;
}

bool Root1ResourceBroker::tryAcquire(ResourceState& state, ParticipantId participant) const {
    if (participant == 0) {
        return false;
    }
    return tryAcquireUnlocked(state, participant);
}

AcquireStatus Root1ResourceBroker::acquireOrQueue(const ResourceKey& key, ResourceState& state, ParticipantId participant) {
    if (participant == 0) {
        return AcquireStatus::InvalidParticipant;
    }

    if (tryAcquireUnlocked(state, participant)) {
        return AcquireStatus::Acquired;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidParticipantLocked(participant)) {
        return AcquireStatus::InvalidParticipant;
    }

    const uint64_t currentOwner = state.owner();
    if (currentOwner == participant) {
        return AcquireStatus::Acquired;
    }

    if (tryAcquireUnlocked(state, participant)) {
        return AcquireStatus::Acquired;
    }

    auto& queue = waiters_[key];
    if (std::find(queue.begin(), queue.end(), participant) == queue.end()) {
        queue.push_back(participant);
    }
    state.word_.fetch_or(ResourceState::kContendedBit, std::memory_order_acq_rel);
    return AcquireStatus::Queued;
}

bool Root1ResourceBroker::release(const ResourceKey& key, ResourceState& state, ParticipantId owner) {
    if (owner == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if ((state.word_.load(std::memory_order_acquire) & ResourceState::kOwnerMask) != owner) {
        return false;
    }

    auto waiterIt = waiters_.find(key);
    while (waiterIt != waiters_.end() && !waiterIt->second.empty()) {
        ParticipantId next = waiterIt->second.front();
        waiterIt->second.pop_front();
        if (!isValidParticipantLocked(next)) {
            continue;
        }

        const bool hasMoreWaiters = !waiterIt->second.empty();
        const uint64_t nextState = (hasMoreWaiters ? ResourceState::kContendedBit : 0) | next;
        state.word_.store(nextState, std::memory_order_release);

        BrokerEvent event;
        event.kind = BrokerEventKind::OwnershipGranted;
        event.resource = key;
        event.grantToken = nextGrantToken_++;
        event.sequence = event.grantToken;

        MailboxDescriptor descriptor;
        descriptor.eventKind = MailboxEventKind::OwnershipGranted;
        descriptor.payloadKind = MailboxPayloadKind::None;
        descriptor.resource = key;
        descriptor.grantToken = event.grantToken;
        descriptor.sequence = event.sequence;
        pushDescriptorLocked(next, descriptor);
        pushEventLocked(next, std::move(event));
        return true;
    }

    if (waiterIt != waiters_.end() && waiterIt->second.empty()) {
        waiters_.erase(waiterIt);
    }

    state.word_.store(0, std::memory_order_release);
    return true;
}

bool Root1ResourceBroker::sendMailboxMessage(ParticipantId participant, std::string payload, uint64_t sequence) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidParticipantLocked(participant)) {
        return false;
    }

    MailboxDescriptor descriptor;
    descriptor.eventKind = MailboxEventKind::Message;
    descriptor.sequence = sequence;
    setInlinePayload(descriptor, payload);
    pushDescriptorLocked(participant, descriptor);

    BrokerEvent event;
    event.kind = BrokerEventKind::MailboxMessage;
    event.sequence = sequence;
    event.payload = std::move(payload);
    pushEventLocked(participant, std::move(event));
    return true;
}

bool Root1ResourceBroker::sendMailboxDescriptor(ParticipantId participant, const MailboxDescriptor& descriptor) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidParticipantLocked(participant)) {
        return false;
    }
    return pushDescriptorLocked(participant, descriptor);
}

std::vector<ParticipantId> Root1ResourceBroker::pollReadyParticipants(int timeoutMs, size_t maxEvents) {
    std::vector<ParticipantId> ready;
    if (maxEvents == 0 || epollFd_ < 0) {
        return ready;
    }

#if defined(__linux__)
    std::vector<epoll_event> events(maxEvents);
    int count = epoll_wait(epollFd_, events.data(), static_cast<int>(events.size()), timeoutMs);
    if (count <= 0) {
        return ready;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < count; ++i) {
        ParticipantId participant = events[static_cast<size_t>(i)].data.u64;
        auto it = participants_.find(participant);
        if (it == participants_.end()) {
            continue;
        }
        drainEventFd(it->second.eventFd);
        ready.push_back(participant);
    }
#endif

    return ready;
}

std::vector<BrokerEvent> Root1ResourceBroker::drainMailbox(ParticipantId participant) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = participants_.find(participant);
    if (it == participants_.end()) {
        return {};
    }

    std::vector<BrokerEvent> events;
    events.swap(it->second.mailbox);
    return events;
}

std::vector<MailboxDescriptor> Root1ResourceBroker::drainMailboxDescriptors(ParticipantId participant) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = participants_.find(participant);
    if (it == participants_.end() || !it->second.descriptorMailbox) {
        return {};
    }

    std::vector<MailboxDescriptor> descriptors;
    MailboxDescriptor descriptor;
    while (it->second.descriptorMailbox->tryPop(descriptor)) {
        descriptors.push_back(descriptor);
    }
    return descriptors;
}

bool Root1ResourceBroker::isValidParticipantLocked(ParticipantId participant) const {
    return participants_.find(participant) != participants_.end();
}

bool Root1ResourceBroker::notifyParticipantLocked(ParticipantId participant) const {
#if defined(__linux__)
    auto it = participants_.find(participant);
    if (it == participants_.end()) {
        return false;
    }
    return eventfd_write(it->second.eventFd, 1) == 0;
#else
    (void)participant;
    return false;
#endif
}

void Root1ResourceBroker::pushEventLocked(ParticipantId participant, BrokerEvent event) {
    auto it = participants_.find(participant);
    if (it == participants_.end()) {
        return;
    }
    it->second.mailbox.push_back(std::move(event));
    notifyParticipantLocked(participant);
}

bool Root1ResourceBroker::pushDescriptorLocked(ParticipantId participant, const MailboxDescriptor& descriptor) {
    auto it = participants_.find(participant);
    if (it == participants_.end() || !it->second.descriptorMailbox) {
        return false;
    }
    if (!it->second.descriptorMailbox->tryPush(descriptor)) {
        return false;
    }
    notifyParticipantLocked(participant);
    return true;
}

bool Root1ResourceBroker::tryAcquireUnlocked(ResourceState& state, ParticipantId participant) const {
    uint64_t expected = 0;
    return state.word_.compare_exchange_strong(expected, participant, std::memory_order_acq_rel, std::memory_order_acquire);
}

void Root1ResourceBroker::closeAllFds() {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : participants_) {
        if (entry.second.eventFd >= 0) {
            close(entry.second.eventFd);
            entry.second.eventFd = -1;
        }
    }
    participants_.clear();
    waiters_.clear();
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }
#endif
}

} // namespace agentc::root1
