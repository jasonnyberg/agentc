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
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace agentc::root1 {

namespace {

size_t mixHash(size_t seed, uint64_t value) {
    seed ^= static_cast<size_t>(value) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
    return seed;
}

#if defined(__linux__)
uint64_t encodeEpollData(ParticipantId participant, bool pidFd) {
    return (participant << 1) | (pidFd ? 1ull : 0ull);
}

ParticipantId decodeEpollParticipant(uint64_t data) {
    return data >> 1;
}

bool decodeEpollIsPidFd(uint64_t data) {
    return (data & 1ull) != 0;
}

int openPidFd(pid_t pid) {
#ifdef SYS_pidfd_open
    return static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
#else
    (void)pid;
    errno = ENOSYS;
    return -1;
#endif
}

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
    event.data.u64 = encodeEpollData(id, false);
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
    event.data.u64 = encodeEpollData(id, false);
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

bool Root1ResourceBroker::reconstructParticipantFromSlab(CoordinationSlab& slab,
                                                         ParticipantId participant,
                                                         bool notifyPending) {
#if defined(__linux__)
    if (participant == 0) {
        return false;
    }

    CoordinationParticipantSlot* slot = slab.findParticipantSlot(participant);
    if (!slot) {
        return false;
    }

    int fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error("Root1ResourceBroker: eventfd failed");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (participants_.find(participant) != participants_.end()) {
        close(fd);
        return true;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = encodeEpollData(participant, false);
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) != 0) {
        close(fd);
        throw std::runtime_error("Root1ResourceBroker: epoll_ctl add failed");
    }

    ParticipantRecord record;
    record.eventFd = fd;
    record.descriptorMailbox = &slot->mailbox;
    participants_.emplace(participant, std::move(record));
    if (participant >= nextParticipant_) {
        nextParticipant_ = participant + 1;
    }
    if (notifyPending && slot->mailbox.approximateSize() > 0) {
        notifyParticipantLocked(participant);
    }
    return true;
#else
    (void)slab;
    (void)participant;
    (void)notifyPending;
    return false;
#endif
}

std::vector<ParticipantId> Root1ResourceBroker::reconstructParticipantsFromSlab(CoordinationSlab& slab,
                                                                                bool notifyPending) {
    std::vector<ParticipantId> reconstructed;
    for (size_t i = 0; i < kCoordinationParticipantSlots; ++i) {
        auto* slot = slab.participantSlot(i);
        if (!slot) {
            continue;
        }
        ParticipantId participant = slot->participantId.load(std::memory_order_acquire);
        if (participant == 0) {
            continue;
        }
        if (reconstructParticipantFromSlab(slab, participant, notifyPending)) {
            reconstructed.push_back(participant);
        }
    }
    return reconstructed;
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

bool Root1ResourceBroker::attachParticipantPid(ParticipantId participant, pid_t pid) {
#if defined(__linux__)
    if (participant == 0 || pid <= 0) {
        return false;
    }

    int pidFd = openPidFd(pid);
    if (pidFd < 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = participants_.find(participant);
    if (it == participants_.end()) {
        close(pidFd);
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.u64 = encodeEpollData(participant, true);
    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, pidFd, &event) != 0) {
        close(pidFd);
        return false;
    }

    if (it->second.pidFd >= 0) {
        epoll_ctl(epollFd_, EPOLL_CTL_DEL, it->second.pidFd, nullptr);
        close(it->second.pidFd);
    }
    it->second.pidFd = pidFd;
    it->second.pidDeathReported = false;
    return true;
#else
    (void)participant;
    (void)pid;
    return false;
#endif
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

bool Root1ResourceBroker::recoverAbandonedResource(const ResourceKey& key,
                                                   ResourceState& state,
                                                   ParticipantId abandonedOwner,
                                                   std::string reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    return recoverAbandonedResourceLocked(key, state, abandonedOwner, reason);
}

bool Root1ResourceBroker::registerLease(const ResourceKey& key,
                                        ResourceState& state,
                                        ParticipantId owner,
                                        uint64_t expiresAtTick) {
    if (owner == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidParticipantLocked(owner)) {
        return false;
    }
    if ((state.word_.load(std::memory_order_acquire) & ResourceState::kOwnerMask) != owner) {
        return false;
    }

    LeaseRecord record;
    record.state = &state;
    record.owner = owner;
    record.expiresAtTick = expiresAtTick;
    leases_[key] = record;
    return true;
}

bool Root1ResourceBroker::renewLease(const ResourceKey& key,
                                     ParticipantId owner,
                                     uint64_t expiresAtTick) {
    if (owner == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = leases_.find(key);
    if (it == leases_.end() || !it->second.state || it->second.owner != owner) {
        return false;
    }
    if ((it->second.state->word_.load(std::memory_order_acquire) & ResourceState::kOwnerMask) != owner) {
        return false;
    }

    it->second.expiresAtTick = expiresAtTick;
    return true;
}

size_t Root1ResourceBroker::heartbeatParticipant(ParticipantId owner, uint64_t expiresAtTick) {
    if (owner == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    size_t renewed = 0;
    for (auto& entry : leases_) {
        auto& lease = entry.second;
        if (lease.owner == owner && lease.state &&
            (lease.state->word_.load(std::memory_order_acquire) & ResourceState::kOwnerMask) == owner) {
            lease.expiresAtTick = expiresAtTick;
            ++renewed;
        }
    }
    return renewed;
}

std::vector<ResourceKey> Root1ResourceBroker::recoverExpiredLeases(uint64_t nowTick,
                                                                   std::string reason) {
    struct ExpiredLease {
        ResourceKey key;
        ResourceState* state = nullptr;
        ParticipantId owner = 0;
    };

    std::vector<ExpiredLease> expired;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = leases_.begin(); it != leases_.end();) {
            if (it->second.expiresAtTick <= nowTick) {
                expired.push_back({it->first, it->second.state, it->second.owner});
                it = leases_.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<ResourceKey> recovered;
    for (const auto& lease : expired) {
        if (lease.state && recoverAbandonedResource(lease.key,
                                                    *lease.state,
                                                    lease.owner,
                                                    reason.empty() ? "resource lease expired" : reason)) {
            recovered.push_back(lease.key);
        }
    }
    return recovered;
}

std::vector<ResourceKey> Root1ResourceBroker::recoverParticipantLeases(ParticipantId owner,
                                                                       std::string reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    return recoverParticipantLeasesLocked(owner, reason.empty() ? "participant owner abandoned" : reason);
}

PublicationLayerId Root1ResourceBroker::leasePublicationLayer(ParticipantId owner, uint64_t expiresAtTick) {
    if (owner == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!isValidParticipantLocked(owner)) {
        return 0;
    }

    PublicationLayerId layerId = nextPublicationLayerId_++;
    while (layerId == 0 || publicationLeases_.find(layerId) != publicationLeases_.end() ||
           publications_.find(layerId) != publications_.end()) {
        layerId = nextPublicationLayerId_++;
    }

    publicationLeases_[layerId] = PublicationLeaseRecord{owner, expiresAtTick};
    return layerId;
}

bool Root1ResourceBroker::renewPublicationLease(PublicationLayerId layerId,
                                                ParticipantId owner,
                                                uint64_t expiresAtTick) {
    if (layerId == 0 || owner == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = publicationLeases_.find(layerId);
    if (it == publicationLeases_.end() || it->second.owner != owner || !isValidParticipantLocked(owner)) {
        return false;
    }
    it->second.expiresAtTick = expiresAtTick;
    return true;
}

bool Root1ResourceBroker::registerPublication(const PublicationDescriptor& publication,
                                              std::string* error) {
    auto fail = [error](const char* message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (publication.layerId == 0) {
        return fail("publication layer id is required");
    }
    if (publication.owner == 0) {
        return fail("publication owner is required");
    }
    if (publication.epoch == 0) {
        return fail("publication epoch is required");
    }
    if (publication.manifestPath.empty() || publication.manifestHash.empty()) {
        return fail("publication manifest path/hash are required");
    }
    if (!publication.immutable || publication.permission != PublicationPermission::ReadOnly) {
        return fail("publication must be immutable and read-only");
    }
    if (publication.rootHandle.layerId != 0 && publication.rootHandle.layerId != publication.layerId) {
        return fail("publication root handle layer mismatch");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto leaseIt = publicationLeases_.find(publication.layerId);
    if (leaseIt == publicationLeases_.end()) {
        return fail("publication layer is not leased");
    }
    if (leaseIt->second.owner != publication.owner) {
        return fail("publication owner does not hold the layer lease");
    }
    if (!isValidParticipantLocked(publication.owner)) {
        return fail("publication owner is not an active participant");
    }

    auto current = publications_.find(publication.layerId);
    if (current != publications_.end() && publication.epoch <= current->second.epoch) {
        return fail("publication epoch is stale");
    }

    PublicationDescriptor stored = publication;
    if (stored.rootHandle.layerId == 0) {
        stored.rootHandle.layerId = stored.layerId;
    }
    publications_[publication.layerId] = std::move(stored);
    if (error) {
        error->clear();
    }
    return true;
}

std::optional<PublicationDescriptor> Root1ResourceBroker::lookupPublication(PublicationLayerId layerId) const {
    if (layerId == 0) {
        return std::nullopt;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (publicationLeases_.find(layerId) == publicationLeases_.end()) {
        return std::nullopt;
    }
    auto it = publications_.find(layerId);
    if (it == publications_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool Root1ResourceBroker::retirePublication(PublicationLayerId layerId, ParticipantId owner) {
    if (layerId == 0 || owner == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto leaseIt = publicationLeases_.find(layerId);
    if (leaseIt == publicationLeases_.end() || leaseIt->second.owner != owner) {
        return false;
    }
    publications_.erase(layerId);
    publicationLeases_.erase(leaseIt);
    return true;
}

std::vector<PublicationLayerId> Root1ResourceBroker::recoverExpiredPublicationLeases(uint64_t nowTick) {
    std::vector<PublicationLayerId> expired;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = publicationLeases_.begin(); it != publicationLeases_.end();) {
        if (it->second.expiresAtTick <= nowTick) {
            expired.push_back(it->first);
            publications_.erase(it->first);
            it = publicationLeases_.erase(it);
        } else {
            ++it;
        }
    }
    return expired;
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

bool Root1ResourceBroker::sendCancellation(ParticipantId participant, uint64_t correlationId, std::string reason) {
    MailboxDescriptor descriptor;
    descriptor.eventKind = MailboxEventKind::Cancelled;
    descriptor.correlationId = correlationId;
    if (!reason.empty() && !setInlinePayload(descriptor, reason)) {
        return false;
    }
    return sendMailboxDescriptor(participant, descriptor);
}

bool Root1ResourceBroker::sendBackpressure(ParticipantId participant, uint64_t correlationId, std::string reason) {
    MailboxDescriptor descriptor;
    descriptor.eventKind = MailboxEventKind::Backpressure;
    descriptor.correlationId = correlationId;
    if (!reason.empty() && !setInlinePayload(descriptor, reason)) {
        return false;
    }
    return sendMailboxDescriptor(participant, descriptor);
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
        const uint64_t data = events[static_cast<size_t>(i)].data.u64;
        ParticipantId participant = decodeEpollParticipant(data);
        const bool pidFdEvent = decodeEpollIsPidFd(data);
        auto it = participants_.find(participant);
        if (it == participants_.end()) {
            continue;
        }

        if (pidFdEvent) {
            if (!it->second.pidDeathReported) {
                it->second.pidDeathReported = true;
                if (it->second.pidFd >= 0) {
                    epoll_ctl(epollFd_, EPOLL_CTL_DEL, it->second.pidFd, nullptr);
                }
                MailboxDescriptor descriptor;
                descriptor.eventKind = MailboxEventKind::OwnerDied;
                descriptor.correlationId = participant;
                setInlinePayload(descriptor, "participant process exited");
                pushDescriptorLocked(participant, descriptor);
                recoverParticipantLeasesLocked(participant, "participant process exited");
            }
        } else {
            drainEventFd(it->second.eventFd);
        }
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

bool Root1ResourceBroker::recoverAbandonedResourceLocked(const ResourceKey& key,
                                                         ResourceState& state,
                                                         ParticipantId abandonedOwner,
                                                         const std::string& reason) {
    if (abandonedOwner == 0) {
        return false;
    }
    if ((state.word_.load(std::memory_order_acquire) & ResourceState::kOwnerMask) != abandonedOwner) {
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

        MailboxDescriptor ownerDied;
        ownerDied.eventKind = MailboxEventKind::OwnerDied;
        ownerDied.resource = key;
        ownerDied.correlationId = abandonedOwner;
        ownerDied.sequence = nextGrantToken_;
        setInlinePayload(ownerDied, reason.empty() ? "resource owner abandoned" : reason);
        pushDescriptorLocked(next, ownerDied);

        BrokerEvent event;
        event.kind = BrokerEventKind::OwnershipGranted;
        event.resource = key;
        event.grantToken = nextGrantToken_++;
        event.sequence = event.grantToken;

        MailboxDescriptor grant;
        grant.eventKind = MailboxEventKind::OwnershipGranted;
        grant.payloadKind = MailboxPayloadKind::None;
        grant.resource = key;
        grant.grantToken = event.grantToken;
        grant.sequence = event.sequence;
        pushDescriptorLocked(next, grant);
        pushEventLocked(next, std::move(event));
        leases_.erase(key);
        return true;
    }

    if (waiterIt != waiters_.end() && waiterIt->second.empty()) {
        waiters_.erase(waiterIt);
    }

    state.word_.store(0, std::memory_order_release);
    leases_.erase(key);
    return true;
}

std::vector<ResourceKey> Root1ResourceBroker::recoverParticipantLeasesLocked(ParticipantId owner,
                                                                             const std::string& reason) {
    std::vector<ResourceKey> ownedKeys;
    for (const auto& entry : leases_) {
        if (entry.second.owner == owner) {
            ownedKeys.push_back(entry.first);
        }
    }

    std::vector<ResourceKey> recovered;
    for (const auto& key : ownedKeys) {
        auto it = leases_.find(key);
        if (it == leases_.end() || !it->second.state) {
            continue;
        }
        ResourceState* state = it->second.state;
        const ParticipantId leaseOwner = it->second.owner;
        if (recoverAbandonedResourceLocked(key, *state, leaseOwner, reason)) {
            recovered.push_back(key);
        }
    }
    return recovered;
}

void Root1ResourceBroker::closeAllFds() {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : participants_) {
        if (entry.second.eventFd >= 0) {
            close(entry.second.eventFd);
            entry.second.eventFd = -1;
        }
        if (entry.second.pidFd >= 0) {
            close(entry.second.pidFd);
            entry.second.pidFd = -1;
        }
    }
    participants_.clear();
    waiters_.clear();
    leases_.clear();
    publicationLeases_.clear();
    publications_.clear();
    if (epollFd_ >= 0) {
        close(epollFd_);
        epollFd_ = -1;
    }
#endif
}

} // namespace agentc::root1
