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

#include "../edict_compiler.h"
#include "../edict_types.h"
#include "../edict_vm.h"
#include "../root1_await_scheduler.h"
#include "../../core/root1_resource_broker.h"
#include "../../listree/listree.h"

using agentc::ListreeValue;
using agentc::edict::EdictCompiler;
using agentc::edict::EdictVM;
using agentc::edict::Root1AwaitScheduler;

namespace {

CPtr<ListreeValue> namedValue(CPtr<ListreeValue> value, const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string textValue(CPtr<ListreeValue> value) {
    if (!value || !value->getData()) {
        return {};
    }
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

size_t listCount(CPtr<ListreeValue> value) {
    size_t count = 0;
    if (!value || !value->isListMode()) {
        return count;
    }
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            ++count;
        }
    });
    return count;
}

CPtr<ListreeValue> listValueAt(CPtr<ListreeValue> value, size_t index) {
    if (!value || !value->isListMode()) {
        return nullptr;
    }
    size_t current = 0;
    CPtr<ListreeValue> found;
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (found || !ref || !ref->getValue()) {
            return;
        }
        if (current == index) {
            found = ref->getValue();
            return;
        }
        ++current;
    });
    return found;
}

} // namespace

TEST(Root1AwaitSchedulerTest, ResumesYieldedVmWhenWaitableReceivesDescriptor) {
#if !defined(__linux__)
    GTEST_SKIP() << "Root1 await scheduler prototype requires Linux eventfd/epoll";
#else
    agentc::root1::Root1ResourceBroker broker;
    auto participant = broker.registerParticipant();
    ASSERT_GT(participant, 0u);

    auto root = agentc::createNullValue();
    EdictVM vm(root);
    EdictCompiler compiler;
    int state = vm.execute(compiler.compile("yield! @events events"));
    ASSERT_TRUE(state & agentc::edict::VM_YIELD);

    Root1AwaitScheduler scheduler;
    ASSERT_GT(scheduler.park(participant, vm), 0u);
    EXPECT_EQ(scheduler.parkedCount(), 1u);

    agentc::root1::MailboxDescriptor descriptor;
    descriptor.eventKind = agentc::root1::MailboxEventKind::Progress;
    descriptor.sequence = 77;
    descriptor.correlationId = 1234;
    ASSERT_TRUE(agentc::root1::setInlinePayload(descriptor, "ready"));
    ASSERT_TRUE(broker.sendMailboxDescriptor(participant, descriptor));

    auto result = scheduler.pollAndResume(broker, 1000);
    EXPECT_EQ(result.resumedContinuations, 1u);
    EXPECT_EQ(scheduler.parkedCount(), 0u);

    auto events = namedValue(root, "events");
    ASSERT_TRUE(events);
    ASSERT_EQ(listCount(events), 1u);
    auto event = listValueAt(events, 0);
    ASSERT_TRUE(event);
    EXPECT_EQ(textValue(namedValue(event, "kind")), "progress");
    EXPECT_EQ(textValue(namedValue(event, "sequence")), "77");
    EXPECT_EQ(textValue(namedValue(event, "correlation_id")), "1234");
    EXPECT_EQ(textValue(namedValue(event, "payload")), "ready");
    EXPECT_EQ(listCount(vm.getStackTop()), 1u);
#endif
}
