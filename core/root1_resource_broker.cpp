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
#include <new>
#include <stdexcept>
#include <utility>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
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

void ResourceState::reset() {
    word_.store(0, std::memory_order_release);
}

std::unique_ptr<CoordinationSlab> CoordinationSlab::createAnonymousForTests() {
#if defined(__linux__)
    const size_t bytes = sizeof(CoordinationSlabLayout);
    void* mapping = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        throw std::runtime_error("CoordinationSlab: anonymous mmap failed");
    }

    auto* layout = new (mapping) CoordinationSlabLayout();
    layout->header.headerBytes = sizeof(CoordinationSlabHeader);
    layout->header.mappingBytes = bytes;
    return std::unique_ptr<CoordinationSlab>(new CoordinationSlab(mapping, bytes, -1, false));
#else
    throw std::runtime_error("CoordinationSlab requires Linux mmap");
#endif
}

std::unique_ptr<CoordinationSlab> CoordinationSlab::createFileBacked(const std::string& path, bool reset) {
#if defined(__linux__)
    const size_t bytes = sizeof(CoordinationSlabLayout);
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        throw std::runtime_error("CoordinationSlab: open failed");
    }

    if (reset && ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        close(fd);
        throw std::runtime_error("CoordinationSlab: ftruncate failed");
    }

    struct stat st{};
    if (fstat(fd, &st) != 0) {
        close(fd);
        throw std::runtime_error("CoordinationSlab: fstat failed");
    }
    if (static_cast<size_t>(st.st_size) < bytes) {
        close(fd);
        throw std::runtime_error("CoordinationSlab: existing file is too small");
    }

    void* mapping = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        close(fd);
        throw std::runtime_error("CoordinationSlab: file mmap failed");
    }

    auto* layout = static_cast<CoordinationSlabLayout*>(mapping);
    if (reset) {
        layout = new (mapping) CoordinationSlabLayout();
        layout->header.headerBytes = sizeof(CoordinationSlabHeader);
        layout->header.mappingBytes = bytes;
        msync(mapping, bytes, MS_SYNC);
    } else if (layout->header.magic != kCoordinationSlabMagic ||
               layout->header.version != kCoordinationSlabVersion ||
               layout->header.mappingBytes != bytes) {
        munmap(mapping, bytes);
        close(fd);
        throw std::runtime_error("CoordinationSlab: incompatible existing mapping");
    }

    return std::unique_ptr<CoordinationSlab>(new CoordinationSlab(mapping, bytes, fd, true));
#else
    (void)path;
    (void)reset;
    throw std::runtime_error("CoordinationSlab requires Linux mmap");
#endif
}

CoordinationSlab::CoordinationSlab(void* mapping, size_t mappingBytes, int fd, bool fileBacked)
    : layout_(static_cast<CoordinationSlabLayout*>(mapping)),
      mappingBytes_(mappingBytes),
      fd_(fd),
      fileBacked_(fileBacked) {}

CoordinationSlab::~CoordinationSlab() {
#if defined(__linux__)
    if (layout_ && mappingBytes_ > 0) {
        if (fileBacked_) {
            msync(layout_, mappingBytes_, MS_SYNC);
        }
        munmap(layout_, mappingBytes_);
        layout_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
#endif
}

bool CoordinationSlab::valid() const {
    return layout_ &&
           layout_->header.magic == kCoordinationSlabMagic &&
           layout_->header.version == kCoordinationSlabVersion &&
           layout_->header.mappingBytes == mappingBytes_;
}

bool CoordinationSlab::flush() {
#if defined(__linux__)
    if (!layout_ || mappingBytes_ == 0) {
        return false;
    }
    return msync(layout_, mappingBytes_, MS_SYNC) == 0;
#else
    return false;
#endif
}

size_t CoordinationSlab::mappingSize() const {
    return mappingBytes_;
}

CoordinationSlabHeader& CoordinationSlab::header() {
    return layout_->header;
}

const CoordinationSlabHeader& CoordinationSlab::header() const {
    return layout_->header;
}

CoordinationParticipantSlot* CoordinationSlab::participantSlot(size_t index) {
    if (!layout_ || index >= layout_->participants.size()) {
        return nullptr;
    }
    return &layout_->participants[index];
}

CoordinationParticipantSlot* CoordinationSlab::findParticipantSlot(ParticipantId participant) {
    if (!layout_ || participant == 0) {
        return nullptr;
    }
    for (auto& slot : layout_->participants) {
        if (slot.participantId.load(std::memory_order_acquire) == participant) {
            return &slot;
        }
    }
    return nullptr;
}

CoordinationParticipantSlot* CoordinationSlab::allocateParticipantSlot(ParticipantId participant) {
    if (!layout_ || participant == 0) {
        return nullptr;
    }
    if (auto* existing = findParticipantSlot(participant)) {
        return existing;
    }
    for (auto& slot : layout_->participants) {
        ParticipantId expected = 0;
        if (slot.participantId.compare_exchange_strong(expected, participant, std::memory_order_acq_rel)) {
            slot.generation.fetch_add(1, std::memory_order_acq_rel);
            slot.mailbox.resetForTests();
            return &slot;
        }
    }
    return nullptr;
}

CoordinationResourceSlot* CoordinationSlab::resourceSlot(size_t index) {
    if (!layout_ || index >= layout_->resources.size()) {
        return nullptr;
    }
    return &layout_->resources[index];
}

CoordinationResourceSlot* CoordinationSlab::findResourceSlot(const ResourceKey& key) {
    if (!layout_) {
        return nullptr;
    }
    for (auto& slot : layout_->resources) {
        if (slot.occupied.load(std::memory_order_acquire) != 0 && slot.key == key) {
            return &slot;
        }
    }
    return nullptr;
}

CoordinationResourceSlot* CoordinationSlab::allocateResourceSlot(const ResourceKey& key) {
    if (!layout_) {
        return nullptr;
    }
    if (auto* existing = findResourceSlot(key)) {
        return existing;
    }
    for (auto& slot : layout_->resources) {
        uint64_t expected = 0;
        if (slot.occupied.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            slot.key = key;
            slot.state.reset();
            return &slot;
        }
    }
    return nullptr;
}

ResourceState* CoordinationSlab::resourceState(const ResourceKey& key) {
    auto* slot = findResourceSlot(key);
    return slot ? &slot->state : nullptr;
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
    record.ownedDescriptorMailbox = std::make_unique<MailboxRing>();
    record.descriptorMailbox = record.ownedDescriptorMailbox.get();
    participants_.emplace(id, std::move(record));
    return id;
#else
    throw std::runtime_error("Root1ResourceBroker requires Linux eventfd/epoll");
#endif
}

ParticipantId Root1ResourceBroker::registerParticipantOnSlab(CoordinationSlab& slab) {
#if defined(__linux__)
    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("Root1ResourceBroker: eventfd failed");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ParticipantId id = nextParticipant_++;
    CoordinationParticipantSlot* slot = slab.allocateParticipantSlot(id);
    if (!slot) {
        close(fd);
        throw std::runtime_error("Root1ResourceBroker: no coordination slab participant slots available");
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = id;
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) != 0) {
        close(fd);
        throw std::runtime_error("Root1ResourceBroker: epoll_ctl add failed");
    }

    ParticipantRecord record;
    record.eventFd = fd;
    record.descriptorMailbox = &slot->mailbox;
    participants_.emplace(id, std::move(record));
    return id;
#else
    (void)slab;
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
