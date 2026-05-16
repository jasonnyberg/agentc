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
using agentc::root1::ResourceKey;
using agentc::root1::ResourceState;
using agentc::root1::Root1ResourceBroker;

namespace {

bool containsParticipant(const std::vector<agentc::root1::ParticipantId>& participants,
                         agentc::root1::ParticipantId participant) {
    return std::find(participants.begin(), participants.end(), participant) != participants.end();
}

} // namespace

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

    ASSERT_TRUE(broker.release(key, state, waiter));
    EXPECT_FALSE(state.isOwned());
    EXPECT_FALSE(state.isContended());
#endif
}

TEST(Root1ResourceBrokerTest, DeliversMailboxDescriptorsThroughEpoll) {
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

    EXPECT_TRUE(broker.drainMailbox(participant).empty());
#endif
}
