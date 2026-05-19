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

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "../edict_compiler.h"
#include "../edict_vm.h"
#include "../../listree/listree.h"

using agentc::ListreeValue;
using agentc::edict::EdictCompiler;
using agentc::edict::EdictVM;
using agentc::edict::VM_ERROR;

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

std::vector<std::string> listStrings(CPtr<ListreeValue> value) {
    std::vector<std::string> out;
    if (!value || !value->isListMode()) {
        return out;
    }
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue()) {
            out.push_back(textValue(ref->getValue()));
        }
    });
    return out;
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

CPtr<ListreeValue> listFindKind(CPtr<ListreeValue> value, const std::string& kind) {
    if (!value || !value->isListMode()) {
        return nullptr;
    }
    CPtr<ListreeValue> found;
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (found || !ref || !ref->getValue()) {
            return;
        }
        if (textValue(namedValue(ref->getValue(), "kind")) == kind) {
            found = ref->getValue();
        }
    });
    return found;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string root1Prelude() {
    const auto edictSourceDir = std::filesystem::path(TEST_EDICT_SOURCE_DIR);
    const auto repoRoot = edictSourceDir.parent_path();
    const auto libedict = std::filesystem::path(TEST_EDICT_BIN_DIR) / "libedict.so";
    const auto primitiveHeader = edictSourceDir / "agentc_root1_primitives.h";
    const auto root1Module = repoRoot / "cpp-agent" / "edict" / "modules" / "root1.edict";

    return std::string("[") + libedict.string() + "] [" + primitiveHeader.string() + "] resolver.import! @root1ffi\n" +
           readTextFile(root1Module) + "\n";
}

} // namespace

TEST(Root1PrimitiveModuleTest, ModuleBackedParticipantMailboxPrimitives) {
    auto root = agentc::createNullValue();
    EdictVM vm(root);
    EdictCompiler compiler;

    int state = vm.execute(compiler.compile(root1Prelude()));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    state = vm.execute(compiler.compile(
        "root1.participant_register! @participant "
        "participant.waitable @waitable "
        "{\"kind\":\"progress\",\"sequence\":\"77\",\"correlation_id\":\"1234\",\"payload\":\"descriptor-progress\"} @descriptor "
        "waitable descriptor root1.mailbox_send! @sent "
        "waitable root1.await! @awaited "
        "[1000] root1.poll! @ready "
        "waitable root1.mailbox_drain! @events"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    auto participant = namedValue(root, "participant");
    ASSERT_TRUE(participant);
    EXPECT_EQ(textValue(namedValue(participant, "state")), "registered");
    EXPECT_EQ(listStrings(namedValue(participant, "ok")), std::vector<std::string>({"ok"}));
    const std::string participantId = textValue(namedValue(participant, "participant_id"));
    ASSERT_FALSE(participantId.empty());
    ASSERT_TRUE(namedValue(participant, "waitable"));
    EXPECT_EQ(textValue(namedValue(namedValue(participant, "waitable"), "kind")), "root1-broker");

    auto sent = namedValue(root, "sent");
    ASSERT_TRUE(sent);
    EXPECT_EQ(textValue(namedValue(sent, "state")), "sent");
    EXPECT_EQ(textValue(namedValue(sent, "participant_id")), participantId);

    auto awaited = namedValue(root, "awaited");
    ASSERT_TRUE(awaited);
    EXPECT_EQ(textValue(namedValue(awaited, "state")), "ready");
    EXPECT_EQ(textValue(namedValue(awaited, "participant_id")), participantId);

    auto ready = namedValue(root, "ready");
    ASSERT_TRUE(ready);
    auto readyParticipants = listStrings(namedValue(ready, "participants"));
    EXPECT_TRUE(readyParticipants.empty());

    auto events = namedValue(awaited, "events");
    ASSERT_TRUE(events);
    ASSERT_EQ(listCount(events), 1u);
    auto descriptor = listValueAt(events, 0);
    ASSERT_TRUE(descriptor);
    EXPECT_EQ(textValue(namedValue(descriptor, "kind")), "progress");
    EXPECT_EQ(textValue(namedValue(descriptor, "sequence")), "77");
    EXPECT_EQ(textValue(namedValue(descriptor, "correlation_id")), "1234");
    EXPECT_EQ(textValue(namedValue(descriptor, "payload")), "descriptor-progress");

    state = vm.execute(compiler.compile(
        "participant {\"correlation_id\":\"2001\",\"reason\":\"stop\"} root1.send_cancellation! @cancel_sent "
        "participant {\"correlation_id\":\"2002\",\"reason\":\"busy\"} root1.send_backpressure! @backpressure_sent "
        "[1000] root1.poll! @ready2 "
        "participant root1.mailbox_drain! @events2"));
    ASSERT_FALSE(state & VM_ERROR) << vm.getError();

    EXPECT_EQ(textValue(namedValue(namedValue(root, "cancel_sent"), "state")), "sent");
    EXPECT_EQ(textValue(namedValue(namedValue(root, "cancel_sent"), "kind")), "cancelled");
    EXPECT_EQ(textValue(namedValue(namedValue(root, "backpressure_sent"), "state")), "sent");
    EXPECT_EQ(textValue(namedValue(namedValue(root, "backpressure_sent"), "kind")), "backpressure");

    auto events2 = namedValue(root, "events2");
    ASSERT_TRUE(events2);
    ASSERT_EQ(listCount(events2), 2u);
    auto cancelled = listFindKind(events2, "cancelled");
    auto backpressure = listFindKind(events2, "backpressure");
    ASSERT_TRUE(cancelled);
    ASSERT_TRUE(backpressure);
    EXPECT_EQ(textValue(namedValue(cancelled, "correlation_id")), "2001");
    EXPECT_EQ(textValue(namedValue(cancelled, "payload")), "stop");
    EXPECT_EQ(textValue(namedValue(backpressure, "correlation_id")), "2002");
    EXPECT_EQ(textValue(namedValue(backpressure, "payload")), "busy");
}
