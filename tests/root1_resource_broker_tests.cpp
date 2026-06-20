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
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

using agentc::root1::AcquireStatus;
using agentc::root1::BrokerEventKind;
using agentc::root1::CoordinationSlab;
using agentc::root1::MailboxDescriptor;
using agentc::root1::MailboxEventKind;
using agentc::root1::MailboxPayloadKind;
using agentc::root1::MailboxRing;
using agentc::root1::PublicationDescriptor;
using agentc::root1::PublicationPermission;
using agentc::root1::ResourceKey;
using agentc::root1::ResourceState;
using agentc::root1::Root1ResourceBroker;

namespace {

bool containsParticipant(const std::vector<agentc::root1::ParticipantId>& participants,
                         agentc::root1::ParticipantId participant) {
    return std::find(participants.begin(), participants.end(), participant) != participants.end();
}

std::string testFnv1a64Hex(std::string_view bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string manifestJson(const PublicationDescriptor& publication) {
    std::ostringstream out;
    out << "{"
        << "\"schema\":\"agentc.root1.publication.v1\","
        << "\"layer_id\":" << publication.layerId << ","
        << "\"epoch\":" << publication.epoch << ","
        << "\"permission\":\"read_only\","
        << "\"immutable\":true,"
        << "\"root_descriptor\":\"" << publication.rootDescriptor << "\","
        << "\"root_layer_id\":" << publication.layerId << ","
        << "\"root_slab_id\":" << publication.rootHandle.slabId << ","
        << "\"root_offset\":" << publication.rootHandle.offset << ","
        << "\"root_generation\":" << publication.rootHandle.generation
        << "}";
    return out.str();
}

void writeTextFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(file) << "failed to open " << path;
    file << content;
    ASSERT_TRUE(file.good()) << "failed to write " << path;
}

PublicationDescriptor makePublication(agentc::root1::PublicationLayerId layerId,
                                      agentc::root1::ParticipantId owner,
                                      uint64_t epoch) {
    PublicationDescriptor publication;
    publication.layerId = layerId;
    publication.owner = owner;
    publication.epoch = epoch;
    publication.rootDescriptor = "worker/result";
    publication.rootHandle.layerId = layerId;
    publication.rootHandle.slabId = 7;
    publication.rootHandle.offset = 11;
    publication.rootHandle.generation = epoch;
    publication.permission = PublicationPermission::ReadOnly;
    publication.immutable = true;

    auto manifestPath = std::filesystem::temp_directory_path() /
                        ("agentc-root1-publication-" + std::to_string(layerId) + "-" +
                         std::to_string(owner) + "-" + std::to_string(epoch) + ".json");
    publication.manifestPath = manifestPath.string();
    const std::string manifest = manifestJson(publication);
    writeTextFile(publication.manifestPath, manifest);
    publication.manifestHash = testFnv1a64Hex(manifest);
    return publication;
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

TEST(Root1ResourceBrokerTest, ReconstructsParticipantEventfdForPendingMappedMailboxAfterRemap) {
#if !defined(__linux__)
    GTEST_SKIP() << "CoordinationSlab prototype requires Linux mmap";
#else
    const auto path = std::filesystem::temp_directory_path() /
        ("agentc_root1_coord_" + std::to_string(::getpid()) + "_reconstruct.bin");
    std::filesystem::remove(path);
    agentc::root1::ParticipantId participant = 0;

    {
        auto slab = CoordinationSlab::createFileBacked(path.string(), true);
        Root1ResourceBroker broker;
        participant = broker.registerParticipantOnSlab(*slab);
        ASSERT_GT(participant, 0u);

        MailboxDescriptor descriptor;
        descriptor.eventKind = MailboxEventKind::Progress;
        descriptor.sequence = 789;
        ASSERT_TRUE(agentc::root1::setInlinePayload(descriptor, "pending-after-remap"));
        ASSERT_TRUE(broker.sendMailboxDescriptor(participant, descriptor));
        ASSERT_TRUE(slab->flush());
    }

    {
        auto slab = CoordinationSlab::createFileBacked(path.string(), false);
        Root1ResourceBroker broker;
        auto reconstructed = broker.reconstructParticipantsFromSlab(*slab);
        ASSERT_EQ(reconstructed.size(), 1u);
        EXPECT_EQ(reconstructed.front(), participant);
        EXPECT_TRUE(broker.hasParticipant(participant));
        EXPECT_GE(broker.participantEventFd(participant), 0);

        auto ready = broker.pollReadyParticipants(1000);
        ASSERT_TRUE(containsParticipant(ready, participant));

        auto descriptors = broker.drainMailboxDescriptors(participant);
        ASSERT_EQ(descriptors.size(), 1u);
        EXPECT_EQ(descriptors.front().eventKind, MailboxEventKind::Progress);
        EXPECT_EQ(descriptors.front().sequence, 789u);
        EXPECT_EQ(agentc::root1::inlinePayload(descriptors.front()), "pending-after-remap");
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

TEST(Root1ResourceBrokerTest, RecoversAbandonedOwnedResourceToQueuedWaiter) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    auto waiter = broker.registerParticipant();

    ResourceState state;
    ResourceKey key{9, 8, 7, 6, 5, 4};

    ASSERT_TRUE(broker.tryAcquire(state, owner));
    ASSERT_EQ(broker.acquireOrQueue(key, state, waiter), AcquireStatus::Queued);
    ASSERT_TRUE(state.isContended());

    ASSERT_TRUE(broker.recoverAbandonedResource(key, state, owner, "owner lease expired"));
    EXPECT_EQ(state.owner(), waiter);

    auto ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, waiter));

    auto descriptors = broker.drainMailboxDescriptors(waiter);
    ASSERT_EQ(descriptors.size(), 2u);
    EXPECT_EQ(descriptors[0].eventKind, MailboxEventKind::OwnerDied);
    EXPECT_EQ(descriptors[0].correlationId, owner);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors[0]), "owner lease expired");
    EXPECT_EQ(descriptors[1].eventKind, MailboxEventKind::OwnershipGranted);
    EXPECT_EQ(descriptors[1].resource, key);
#endif
}

TEST(Root1ResourceBrokerTest, RecoversAbandonedOwnedResourceToUnownedWhenNoWaiterExists) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();

    ResourceState state;
    ResourceKey key{10, 8, 7, 6, 5, 4};

    ASSERT_TRUE(broker.tryAcquire(state, owner));
    ASSERT_TRUE(broker.recoverAbandonedResource(key, state, owner, "owner lease expired"));
    EXPECT_FALSE(state.isOwned());
    EXPECT_FALSE(state.isContended());
#endif
}

TEST(Root1ResourceBrokerTest, RecoversExpiredLeaseToQueuedWaiter) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    auto waiter = broker.registerParticipant();

    ResourceState state;
    ResourceKey key{11, 8, 7, 6, 5, 4};

    ASSERT_TRUE(broker.tryAcquire(state, owner));
    ASSERT_TRUE(broker.registerLease(key, state, owner, 10));
    ASSERT_EQ(broker.acquireOrQueue(key, state, waiter), AcquireStatus::Queued);

    auto recovered = broker.recoverExpiredLeases(10, "lease expired");
    ASSERT_EQ(recovered.size(), 1u);
    EXPECT_EQ(recovered.front(), key);
    EXPECT_EQ(state.owner(), waiter);

    auto ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, waiter));
    auto descriptors = broker.drainMailboxDescriptors(waiter);
    ASSERT_EQ(descriptors.size(), 2u);
    EXPECT_EQ(descriptors[0].eventKind, MailboxEventKind::OwnerDied);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors[0]), "lease expired");
    EXPECT_EQ(descriptors[1].eventKind, MailboxEventKind::OwnershipGranted);
#endif
}

TEST(Root1ResourceBrokerTest, RenewedLeasePreventsPrematureRecovery) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();

    ResourceState state;
    ResourceKey key{12, 8, 7, 6, 5, 4};

    ASSERT_TRUE(broker.tryAcquire(state, owner));
    ASSERT_TRUE(broker.registerLease(key, state, owner, 10));
    ASSERT_TRUE(broker.renewLease(key, owner, 100));

    EXPECT_TRUE(broker.recoverExpiredLeases(50, "lease expired").empty());
    EXPECT_EQ(state.owner(), owner);

    auto recovered = broker.recoverExpiredLeases(100, "lease expired");
    ASSERT_EQ(recovered.size(), 1u);
    EXPECT_EQ(recovered.front(), key);
    EXPECT_FALSE(state.isOwned());
    EXPECT_FALSE(state.isContended());
#endif
}

TEST(Root1ResourceBrokerTest, ParticipantHeartbeatRenewsOwnedLeases) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();

    ResourceState first;
    ResourceState second;
    ResourceKey firstKey{14, 8, 7, 6, 5, 4};
    ResourceKey secondKey{15, 8, 7, 6, 5, 4};

    ASSERT_TRUE(broker.tryAcquire(first, owner));
    ASSERT_TRUE(broker.tryAcquire(second, owner));
    ASSERT_TRUE(broker.registerLease(firstKey, first, owner, 10));
    ASSERT_TRUE(broker.registerLease(secondKey, second, owner, 10));

    EXPECT_EQ(broker.heartbeatParticipant(owner, 100), 2u);
    EXPECT_TRUE(broker.recoverExpiredLeases(50, "lease expired").empty());
    EXPECT_EQ(first.owner(), owner);
    EXPECT_EQ(second.owner(), owner);
#endif
}

TEST(Root1ResourceBrokerTest, RecoversAllLeasesForOwnerParticipant) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    auto waiter = broker.registerParticipant();

    ResourceState state;
    ResourceKey key{16, 8, 7, 6, 5, 4};

    ASSERT_TRUE(broker.tryAcquire(state, owner));
    ASSERT_TRUE(broker.registerLease(key, state, owner, 100));
    ASSERT_EQ(broker.acquireOrQueue(key, state, waiter), AcquireStatus::Queued);

    auto recovered = broker.recoverParticipantLeases(owner, "owner heartbeat lost");
    ASSERT_EQ(recovered.size(), 1u);
    EXPECT_EQ(recovered.front(), key);
    EXPECT_EQ(state.owner(), waiter);

    auto ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, waiter));
    auto descriptors = broker.drainMailboxDescriptors(waiter);
    ASSERT_EQ(descriptors.size(), 2u);
    EXPECT_EQ(descriptors[0].eventKind, MailboxEventKind::OwnerDied);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors[0]), "owner heartbeat lost");
    EXPECT_EQ(descriptors[1].eventKind, MailboxEventKind::OwnershipGranted);
#endif
}

TEST(Root1ResourceBrokerTest, RejectsLeaseForMismatchedOwner) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    auto other = broker.registerParticipant();

    ResourceState state;
    ResourceKey key{13, 8, 7, 6, 5, 4};

    ASSERT_TRUE(broker.tryAcquire(state, owner));
    EXPECT_FALSE(broker.registerLease(key, state, other, 10));
    EXPECT_FALSE(broker.renewLease(key, other, 20));
    EXPECT_TRUE(broker.recoverExpiredLeases(10, "lease expired").empty());
    EXPECT_EQ(state.owner(), owner);
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

TEST(Root1ResourceBrokerTest, LeasesLogicalPublicationLayerAndPublishesReadOnlyManifest) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    ASSERT_GT(owner, 0u);

    auto layer = broker.leasePublicationLayer(owner, 100);
    ASSERT_GT(layer, 0u);
    auto publication = makePublication(layer, owner, 1);
    publication.rootHandle.layerId = 0; // Root1 should normalize an omitted handle layer.

    std::string error;
    ASSERT_TRUE(broker.registerPublication(publication, &error)) << error;
    EXPECT_TRUE(error.empty());

    auto visible = broker.lookupPublication(layer);
    ASSERT_TRUE(visible.has_value());
    EXPECT_EQ(visible->layerId, layer);
    EXPECT_EQ(visible->owner, owner);
    EXPECT_EQ(visible->epoch, 1u);
    EXPECT_EQ(visible->manifestPath, publication.manifestPath);
    EXPECT_EQ(visible->manifestHash, publication.manifestHash);
    EXPECT_EQ(visible->rootDescriptor, "worker/result");
    EXPECT_EQ(visible->rootHandle.layerId, layer);
    EXPECT_EQ(visible->permission, PublicationPermission::ReadOnly);
    EXPECT_TRUE(visible->immutable);

    EXPECT_TRUE(broker.renewPublicationLease(layer, owner, 200));
    EXPECT_TRUE(broker.retirePublication(layer, owner));
    EXPECT_FALSE(broker.lookupPublication(layer).has_value());
#endif
}

TEST(Root1ResourceBrokerTest, PublicationRegistryRejectsCollisionsAndStaleEpochs) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    auto other = broker.registerParticipant();
    ASSERT_GT(owner, 0u);
    ASSERT_GT(other, 0u);

    auto ownerLayer = broker.leasePublicationLayer(owner, 100);
    auto otherLayer = broker.leasePublicationLayer(other, 100);
    ASSERT_GT(ownerLayer, 0u);
    ASSERT_GT(otherLayer, 0u);
    EXPECT_NE(ownerLayer, otherLayer) << "Root1 must assign distinct logical publication layers";

    std::string error;
    auto wrongOwner = makePublication(ownerLayer, other, 1);
    EXPECT_FALSE(broker.registerPublication(wrongOwner, &error));
    EXPECT_EQ(error, "publication owner does not hold the layer lease");

    auto missingManifest = makePublication(ownerLayer, owner, 1);
    missingManifest.manifestHash.clear();
    EXPECT_FALSE(broker.registerPublication(missingManifest, &error));
    EXPECT_EQ(error, "publication manifest path/hash are required");

    auto first = makePublication(ownerLayer, owner, 2);
    ASSERT_TRUE(broker.registerPublication(first, &error)) << error;

    auto stale = makePublication(ownerLayer, owner, 2);
    EXPECT_FALSE(broker.registerPublication(stale, &error));
    EXPECT_EQ(error, "publication epoch is stale");

    auto newer = makePublication(ownerLayer, owner, 3);
    EXPECT_TRUE(broker.registerPublication(newer, &error)) << error;
    auto visible = broker.lookupPublication(ownerLayer);
    ASSERT_TRUE(visible.has_value());
    EXPECT_EQ(visible->epoch, 3u);
#endif
}

TEST(Root1ResourceBrokerTest, PublicationRegistryValidatesManifestFileHashAndRootMetadata) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    ASSERT_GT(owner, 0u);
    auto layer = broker.leasePublicationLayer(owner, 100);
    ASSERT_GT(layer, 0u);

    std::string error;
    auto missingFile = makePublication(layer, owner, 1);
    missingFile.manifestPath += ".missing";
    EXPECT_FALSE(broker.registerPublication(missingFile, &error));
    EXPECT_EQ(error, "publication manifest cannot be opened");

    auto badHash = makePublication(layer, owner, 1);
    badHash.manifestHash = "fnv1a64:0000000000000000";
    EXPECT_FALSE(broker.registerPublication(badHash, &error));
    EXPECT_EQ(error, "publication manifest hash mismatch");

    auto wrongRoot = makePublication(layer, owner, 1);
    const std::string mismatchedManifest =
        "{\"schema\":\"agentc.root1.publication.v1\","
        "\"layer_id\":" + std::to_string(layer) + ","
        "\"epoch\":1,"
        "\"permission\":\"read_only\","
        "\"immutable\":true,"
        "\"root_descriptor\":\"worker/result\","
        "\"root_layer_id\":" + std::to_string(layer) + ","
        "\"root_slab_id\":99,"
        "\"root_offset\":11,"
        "\"root_generation\":1}";
    writeTextFile(wrongRoot.manifestPath, mismatchedManifest);
    wrongRoot.manifestHash = testFnv1a64Hex(mismatchedManifest);
    EXPECT_FALSE(broker.registerPublication(wrongRoot, &error));
    EXPECT_EQ(error, "publication manifest root slab mismatch");

    auto valid = makePublication(layer, owner, 1);
    EXPECT_TRUE(broker.registerPublication(valid, &error)) << error;
#endif
}

TEST(Root1ResourceBrokerTest, ExpiredPublicationLeaseWithdrawsConsumerVisibility) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    auto layer = broker.leasePublicationLayer(owner, 10);
    ASSERT_GT(layer, 0u);

    std::string error;
    ASSERT_TRUE(broker.registerPublication(makePublication(layer, owner, 1), &error)) << error;
    ASSERT_TRUE(broker.lookupPublication(layer).has_value());

    EXPECT_TRUE(broker.recoverExpiredPublicationLeases(9).empty());
    EXPECT_TRUE(broker.lookupPublication(layer).has_value());

    auto expired = broker.recoverExpiredPublicationLeases(10);
    ASSERT_EQ(expired.size(), 1u);
    EXPECT_EQ(expired.front(), layer);
    EXPECT_FALSE(broker.lookupPublication(layer).has_value());
    EXPECT_FALSE(broker.renewPublicationLease(layer, owner, 20));
#endif
}

TEST(Root1ResourceBrokerTest, DeliversCancellationAndBackpressureDescriptors) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux eventfd/epoll";
#else
    Root1ResourceBroker broker;
    auto participant = broker.registerParticipant();

    ASSERT_TRUE(broker.sendCancellation(participant, 1001, "cancel requested"));
    ASSERT_TRUE(broker.sendBackpressure(participant, 1002, "mailbox full"));

    auto ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, participant));

    auto descriptors = broker.drainMailboxDescriptors(participant);
    ASSERT_EQ(descriptors.size(), 2u);
    EXPECT_EQ(descriptors[0].eventKind, MailboxEventKind::Cancelled);
    EXPECT_EQ(descriptors[0].correlationId, 1001u);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors[0]), "cancel requested");
    EXPECT_EQ(descriptors[1].eventKind, MailboxEventKind::Backpressure);
    EXPECT_EQ(descriptors[1].correlationId, 1002u);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors[1]), "mailbox full");
#endif
}

TEST(Root1ResourceBrokerTest, PidfdOwnerDeathRecoversOwnedLeasesWhenAvailable) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux pidfd/eventfd/epoll";
#else
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        _exit(0);
    }

    Root1ResourceBroker broker;
    auto owner = broker.registerParticipant();
    auto waiter = broker.registerParticipant();
    if (!broker.attachParticipantPid(owner, child)) {
        int status = 0;
        waitpid(child, &status, 0);
        GTEST_SKIP() << "pidfd_open is not available in this kernel/container";
    }

    ResourceState state;
    ResourceKey key{17, 8, 7, 6, 5, 4};
    ASSERT_TRUE(broker.tryAcquire(state, owner));
    ASSERT_TRUE(broker.registerLease(key, state, owner, 100));
    ASSERT_EQ(broker.acquireOrQueue(key, state, waiter), AcquireStatus::Queued);

    auto ready = broker.pollReadyParticipants(3000);
    int status = 0;
    waitpid(child, &status, 0);

    ASSERT_TRUE(containsParticipant(ready, owner));
    EXPECT_EQ(state.owner(), waiter);

    ready = broker.pollReadyParticipants(1000);
    ASSERT_TRUE(containsParticipant(ready, waiter));
    auto descriptors = broker.drainMailboxDescriptors(waiter);
    ASSERT_EQ(descriptors.size(), 2u);
    EXPECT_EQ(descriptors[0].eventKind, MailboxEventKind::OwnerDied);
    EXPECT_EQ(descriptors[0].correlationId, owner);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors[0]), "participant process exited");
    EXPECT_EQ(descriptors[1].eventKind, MailboxEventKind::OwnershipGranted);
#endif
}

TEST(Root1ResourceBrokerTest, ReportsOwnerDeathThroughPidfdDescriptorWhenAvailable) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1ResourceBroker prototype requires Linux pidfd/eventfd/epoll";
#else
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        _exit(0);
    }

    Root1ResourceBroker broker;
    auto participant = broker.registerParticipant();
    if (!broker.attachParticipantPid(participant, child)) {
        int status = 0;
        waitpid(child, &status, 0);
        GTEST_SKIP() << "pidfd_open is not available in this kernel/container";
    }

    auto ready = broker.pollReadyParticipants(3000);
    int status = 0;
    waitpid(child, &status, 0);

    ASSERT_TRUE(containsParticipant(ready, participant));
    auto descriptors = broker.drainMailboxDescriptors(participant);
    ASSERT_EQ(descriptors.size(), 1u);
    EXPECT_EQ(descriptors.front().eventKind, MailboxEventKind::OwnerDied);
    EXPECT_EQ(descriptors.front().correlationId, participant);
    EXPECT_EQ(agentc::root1::inlinePayload(descriptors.front()), "participant process exited");
#endif
}
