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

using agentc::root1::AcquireStatus;
using agentc::root1::BrokerEventKind;
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
