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

#include <gtest/gtest.h>

#include "core/root1_resource_broker.h"

#include <algorithm>
#include <filesystem>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#endif

using agentc::root1::AcquireStatus;
using agentc::root1::BrokerEventKind;
using agentc::root1::CoordinationSlab;
using agentc::root1::MailboxDescriptor;
using agentc::root1::MailboxEventKind;
using agentc::root1::MailboxPayloadKind;
using agentc::root1::MailboxRing;
using agentc::root1::ResourceKey;
using agentc::root1::ResourceState;
using agentc::root1::Root1ResourceBroker;

namespace {

bool containsParticipant(const std::vector<agentc::root1::ParticipantId>& participants,
                         agentc::root1::ParticipantId participant) {
    return std::find(participants.begin(), participants.end(), participant) != participants.end();
}

} // namespace

TEST(Root1ResourceBrokerTest, MailboxRingIsBoundedAndPreservesDescriptorOrder) {
    MailboxRing ring;
    EXPECT_TRUE(ring.empty());
    EXPECT_FALSE(ring.full());

    for (size_t i = 0; i < ring.capacity(); ++i) {
        MailboxDescriptor descriptor;
        descriptor.eventKind = MailboxEventKind::Progress;
        descriptor.payloadKind = MailboxPayloadKind::InlineBytes;
        descriptor.sequence = static_cast<uint64_t>(i + 1);
        ASSERT_TRUE(agentc::root1::setInlinePayload(descriptor, "tick"));
        ASSERT_TRUE(ring.tryPush(descriptor));
    }

    EXPECT_TRUE(ring.full());
    MailboxDescriptor overflow;
    overflow.sequence = 999;
    EXPECT_FALSE(ring.tryPush(overflow));

    for (size_t i = 0; i < ring.capacity(); ++i) {
        MailboxDescriptor descriptor;
        ASSERT_TRUE(ring.tryPop(descriptor));
        EXPECT_EQ(descriptor.eventKind, MailboxEventKind::Progress);
        EXPECT_EQ(descriptor.payloadKind, MailboxPayloadKind::InlineBytes);
        EXPECT_EQ(descriptor.sequence, static_cast<uint64_t>(i + 1));
        EXPECT_EQ(agentc::root1::inlinePayload(descriptor), "tick");
    }

    MailboxDescriptor empty;
    EXPECT_FALSE(ring.tryPop(empty));
    EXPECT_TRUE(ring.empty());
}

TEST(Root1ResourceBrokerTest, FileBackedCoordinationSlabPersistsMailboxDescriptorsAcrossRemap) {
#if !defined(__linux__)
    GTEST_SKIP() << "CoordinationSlab prototype requires Linux mmap";
#else
    const auto path = std::filesystem::temp_directory_path() /
        ("agentc_root1_coord_" + std::to_string(::getpid()) + "_mailbox.bin");
    std::filesystem::remove(path);

    {
        auto slab = CoordinationSlab::createFileBacked(path.string(), true);
        ASSERT_TRUE(slab->valid());
        EXPECT_EQ(slab->header().magic, agentc::root1::kCoordinationSlabMagic);
        EXPECT_EQ(slab->header().version, agentc::root1::kCoordinationSlabVersion);
        EXPECT_EQ(slab->header().participantSlots, agentc::root1::kCoordinationParticipantSlots);
        EXPECT_EQ(slab->mappingSize(), slab->header().mappingBytes);

        auto* slot = slab->allocateParticipantSlot(77);
        ASSERT_NE(slot, nullptr);
        MailboxDescriptor descriptor;
        descriptor.eventKind = MailboxEventKind::Progress;
        descriptor.sequence = 123;
        ASSERT_TRUE(agentc::root1::setInlinePayload(descriptor, "persisted-descriptor"));
        ASSERT_TRUE(slot->mailbox.tryPush(descriptor));
        ASSERT_TRUE(slab->flush());
    }

    {
        auto slab = CoordinationSlab::createFileBacked(path.string(), false);
        ASSERT_TRUE(slab->valid());
        auto* slot = slab->findParticipantSlot(77);
        ASSERT_NE(slot, nullptr);
        MailboxDescriptor descriptor;
        ASSERT_TRUE(slot->mailbox.tryPop(descriptor));
        EXPECT_EQ(descriptor.eventKind, MailboxEventKind::Progress);
        EXPECT_EQ(descriptor.sequence, 123u);
        EXPECT_EQ(agentc::root1::inlinePayload(descriptor), "persisted-descriptor");
    }

    std::filesystem::remove(path);
#endif
}

TEST(Root1ResourceBrokerTest, BrokerWritesDescriptorsIntoFileBackedCoordinationSlabMailbox) {
#if !defined(__linux__)
    GTEST_SKIP() << "CoordinationSlab prototype requires Linux mmap";
#else
    const auto path = std::filesystem::temp_directory_path() /
        ("agentc_root1_coord_" + std::to_string(::getpid()) + "_broker_mailbox.bin");
    std::filesystem::remove(path);
    agentc::root1::ParticipantId participant = 0;

    {
        auto slab = CoordinationSlab::createFileBacked(path.string(), true);
        Root1ResourceBroker broker;
        participant = broker.registerParticipantOnSlab(*slab);
        ASSERT_GT(participant, 0u);

        MailboxDescriptor descriptor;
        descriptor.eventKind = MailboxEventKind::Progress;
        descriptor.sequence = 456;
        ASSERT_TRUE(agentc::root1::setInlinePayload(descriptor, "mapped-broker-descriptor"));
        ASSERT_TRUE(broker.sendMailboxDescriptor(participant, descriptor));

        auto ready = broker.pollReadyParticipants(1000);
        ASSERT_TRUE(containsParticipant(ready, participant));
        ASSERT_TRUE(slab->flush());
    }

    {
        auto slab = CoordinationSlab::createFileBacked(path.string(), false);
        auto* slot = slab->findParticipantSlot(participant);
        ASSERT_NE(slot, nullptr);
        MailboxDescriptor descriptor;
        ASSERT_TRUE(slot->mailbox.tryPop(descriptor));
        EXPECT_EQ(descriptor.eventKind, MailboxEventKind::Progress);
        EXPECT_EQ(descriptor.sequence, 456u);
        EXPECT_EQ(agentc::root1::inlinePayload(descriptor), "mapped-broker-descriptor");
    }

    std::filesystem::remove(path);
#endif
}

TEST(Root1ResourceBrokerTest, BrokerCanUseMappedResourceStateForContendedGrant) {
#if !defined(__linux__)
    GTEST_SKIP() << "CoordinationSlab prototype requires Linux mmap";
#else
    auto slab = CoordinationSlab::createAnonymousForTests();
    ASSERT_TRUE(slab->valid());

    ResourceKey key{9, 8, 7, 6, 5, 4};
    auto* resource = slab->allocateResourceSlot(key);
    ASSERT_NE(resource, nullptr);
    ASSERT_NE(slab->resourceState(key), nullptr);

    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    auto waiter = broker.registerParticipant();

    ASSERT_TRUE(broker.tryAcquire(resource->state, owner));
    ASSERT_EQ(broker.acquireOrQueue(key, resource->state, waiter), AcquireStatus::Queued);
    ASSERT_TRUE(broker.release(key, resource->state, owner));

    auto ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, waiter));
    auto descriptors = broker.drainMailboxDescriptors(waiter);
    ASSERT_EQ(descriptors.size(), 1u);
    EXPECT_EQ(descriptors.front().eventKind, MailboxEventKind::OwnershipGranted);
    EXPECT_EQ(descriptors.front().resource, key);
    EXPECT_EQ(resource->state.owner(), waiter);
#endif
}

TEST(Root1ResourceBrokerTest, GrantsContendedResourceThroughParticipantEventfd) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    ASSERT_TRUE(broker.available());

    auto owner = broker.registerParticipant();
    auto waiter = broker.registerParticipant();
    ASSERT_GT(owner, 0u);
    ASSERT_GT(waiter, 0u);
    ASSERT_NE(owner, waiter);
    ASSERT_GE(broker.participantEventFd(owner), 0);
    ASSERT_GE(broker.participantEventFd(waiter), 0);

    ResourceState state;
    ResourceKey key{1, 2, 3, 4, 5, 6};

    EXPECT_TRUE(broker.tryAcquire(state, owner));
    EXPECT_TRUE(state.isOwned());
    EXPECT_EQ(state.owner(), owner);
    EXPECT_FALSE(state.isContended());

    EXPECT_EQ(broker.acquireOrQueue(key, state, waiter), AcquireStatus::Queued);
    EXPECT_EQ(state.owner(), owner);
    EXPECT_TRUE(state.isContended());

    ASSERT_TRUE(broker.release(key, state, owner));
    EXPECT_EQ(state.owner(), waiter);
    EXPECT_TRUE(state.isOwned());

    auto ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, waiter));

    auto events = broker.drainMailbox(waiter);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().kind, BrokerEventKind::OwnershipGranted);
    EXPECT_EQ(events.front().resource, key);
    EXPECT_GT(events.front().grantToken, 0u);

    auto descriptors = broker.drainMailboxDescriptors(waiter);
    ASSERT_EQ(descriptors.size(), 1u);
    EXPECT_EQ(descriptors.front().eventKind, MailboxEventKind::OwnershipGranted);
    EXPECT_EQ(descriptors.front().resource, key);
    EXPECT_EQ(descriptors.front().grantToken, events.front().grantToken);

    ASSERT_TRUE(broker.release(key, state, waiter));
    EXPECT_FALSE(state.isOwned());
    EXPECT_FALSE(state.isContended());
#endif
}

TEST(Root1ResourceBrokerTest, DeliversLegacyMailboxMessagesThroughEpoll) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto participant = broker.registerParticipant();

    ASSERT_TRUE(broker.sendMailboxMessage(participant, "worker-progress", 42));

    auto ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, participant));

    auto events = broker.drainMailbox(participant);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().kind, BrokerEventKind::MailboxMessage);
    EXPECT_EQ(events.front().payload, "worker-progress");
    EXPECT_EQ(events.front().sequence, 42u);

    auto descriptors = broker.drainMailboxDescriptors(participant);
    ASSERT_EQ(descriptors.size(), 1u);
    EXPECT_EQ(descriptors.front().eventKind, MailboxEventKind::Message);
    EXPECT_EQ(descriptors.front().sequence, 42u);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors.front()), "worker-progress");

    EXPECT_TRUE(broker.drainMailbox(participant).empty());
#endif
}

TEST(Root1ResourceBrokerTest, DeliversMmapCompatibleMailboxDescriptorsThroughEpoll) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto participant = broker.registerParticipant();

    MailboxDescriptor descriptor;
    descriptor.eventKind = MailboxEventKind::Progress;
    descriptor.sequence = 77;
    descriptor.correlationId = 1234;
    ASSERT_TRUE(agentc::root1::setInlinePayload(descriptor, "descriptor-progress"));

    ASSERT_TRUE(broker.sendMailboxDescriptor(participant, descriptor));

    auto ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, participant));

    auto descriptors = broker.drainMailboxDescriptors(participant);
    ASSERT_EQ(descriptors.size(), 1u);
    EXPECT_EQ(descriptors.front().eventKind, MailboxEventKind::Progress);
    EXPECT_EQ(descriptors.front().payloadKind, MailboxPayloadKind::InlineBytes);
    EXPECT_EQ(descriptors.front().sequence, 77u);
    EXPECT_EQ(descriptors.front().correlationId, 1234u);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors.front()), "descriptor-progress");
#endif
}
