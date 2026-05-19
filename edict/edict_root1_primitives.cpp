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

#include "agentc_root1_primitives.h"
#include "../cartographer/ltv_api.h"
#include "../core/root1_resource_broker.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace agentc::edict::root1 {
namespace {

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value,
                                      const std::string& name) {
    if (!value || value->isListMode()) {
        return nullptr;
    }
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

bool valueToString(CPtr<agentc::ListreeValue> value, std::string& out) {
    if (!value || !value->getData() || value->getLength() == 0) {
        return false;
    }
    if ((value->getFlags() & agentc::LtvFlags::Binary) != agentc::LtvFlags::None) {
        return false;
    }
    out.assign(static_cast<const char*>(value->getData()), value->getLength());
    return true;
}

std::string stringField(CPtr<agentc::ListreeValue> value,
                        const std::string& name) {
    std::string out;
    valueToString(namedValue(value, name), out);
    return out;
}

bool parseU64(const std::string& text, uint64_t& out) {
    if (text.empty()) {
        return false;
    }
    try {
        size_t parsedChars = 0;
        const unsigned long long parsed = std::stoull(text, &parsedChars, 10);
        if (parsedChars != text.size()) {
            return false;
        }
        out = static_cast<uint64_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool u64Field(CPtr<agentc::ListreeValue> value,
              const std::string& name,
              uint64_t& out) {
    return parseU64(stringField(value, name), out);
}

CPtr<agentc::ListreeValue> statusList(bool ok) {
    auto list = agentc::createListValue();
    if (ok) {
        agentc::addListItem(list, agentc::createStringValue("ok"));
    }
    return list;
}

CPtr<agentc::ListreeValue> errorObject(const std::string& code,
                                       const std::string& message) {
    auto error = agentc::createNullValue();
    agentc::addNamedItem(error, "code", agentc::createStringValue(code));
    agentc::addNamedItem(error, "message", agentc::createStringValue(message));
    return error;
}

CPtr<agentc::ListreeValue> envelope(const std::string& state,
                                    bool ok,
                                    const std::string& errorCode = {},
                                    const std::string& errorMessage = {}) {
    auto value = agentc::createNullValue();
    agentc::addNamedItem(value, "ok", statusList(ok));
    agentc::addNamedItem(value, "state", agentc::createStringValue(state));
    if (ok) {
        agentc::addNamedItem(value, "error", agentc::createNullValue());
    } else {
        agentc::addNamedItem(value, "error", errorObject(errorCode, errorMessage));
    }
    return value;
}

agentc::root1::Root1ResourceBroker& broker() {
    // Process-lifetime broker for first importable Root1 primitive slice.  The
    // broker owns transient eventfds/epoll registrations; Edict sees only
    // logical participant ids, waitables, and mailbox descriptor values.
    static auto* instance = new agentc::root1::Root1ResourceBroker();
    return *instance;
}

std::string descriptorEventName(agentc::root1::MailboxEventKind kind) {
    using agentc::root1::MailboxEventKind;
    switch (kind) {
        case MailboxEventKind::None: return "none";
        case MailboxEventKind::Message: return "message";
        case MailboxEventKind::OwnershipGranted: return "ownership_granted";
        case MailboxEventKind::Progress: return "progress";
        case MailboxEventKind::Complete: return "complete";
        case MailboxEventKind::Error: return "error";
        case MailboxEventKind::Cancelled: return "cancelled";
        case MailboxEventKind::Backpressure: return "backpressure";
        case MailboxEventKind::OwnerDied: return "owner_died";
    }
    return "unknown";
}

bool eventKindFromName(const std::string& name,
                       agentc::root1::MailboxEventKind& out) {
    using agentc::root1::MailboxEventKind;
    if (name == "none") { out = MailboxEventKind::None; return true; }
    if (name == "message") { out = MailboxEventKind::Message; return true; }
    if (name == "ownership_granted") { out = MailboxEventKind::OwnershipGranted; return true; }
    if (name == "progress") { out = MailboxEventKind::Progress; return true; }
    if (name == "complete") { out = MailboxEventKind::Complete; return true; }
    if (name == "error") { out = MailboxEventKind::Error; return true; }
    if (name == "cancelled") { out = MailboxEventKind::Cancelled; return true; }
    if (name == "backpressure") { out = MailboxEventKind::Backpressure; return true; }
    if (name == "owner_died") { out = MailboxEventKind::OwnerDied; return true; }
    return false;
}

CPtr<agentc::ListreeValue> descriptorToValue(const agentc::root1::MailboxDescriptor& descriptor) {
    auto value = agentc::createNullValue();
    agentc::addNamedItem(value, "kind", agentc::createStringValue(descriptorEventName(descriptor.eventKind)));
    agentc::addNamedItem(value, "sequence", agentc::createStringValue(std::to_string(descriptor.sequence)));
    agentc::addNamedItem(value, "correlation_id", agentc::createStringValue(std::to_string(descriptor.correlationId)));
    agentc::addNamedItem(value, "grant_token", agentc::createStringValue(std::to_string(descriptor.grantToken)));
    const auto payload = agentc::root1::inlinePayload(descriptor);
    if (!payload.empty()) {
        agentc::addNamedItem(value, "payload", agentc::createStringValue(std::string(payload)));
    } else {
        agentc::addNamedItem(value, "payload", agentc::createNullValue());
    }
    return value;
}

CPtr<agentc::ListreeValue> descriptorsToList(const std::vector<agentc::root1::MailboxDescriptor>& descriptors) {
    auto list = agentc::createListValue();
    for (const auto& descriptor : descriptors) {
        agentc::addListItem(list, descriptorToValue(descriptor));
    }
    return list;
}

bool descriptorFromValue(CPtr<agentc::ListreeValue> value,
                         agentc::root1::MailboxDescriptor& descriptor,
                         std::string& error) {
    std::string kindName = stringField(value, "kind");
    if (kindName.empty()) {
        kindName = "message";
    }
    if (!eventKindFromName(kindName, descriptor.eventKind)) {
        error = "unknown Root1 mailbox descriptor kind: " + kindName;
        return false;
    }

    uint64_t parsed = 0;
    if (u64Field(value, "sequence", parsed)) {
        descriptor.sequence = parsed;
    }
    if (u64Field(value, "correlation_id", parsed)) {
        descriptor.correlationId = parsed;
    }
    if (u64Field(value, "grant_token", parsed)) {
        descriptor.grantToken = parsed;
    }

    std::string payload = stringField(value, "payload");
    if (payload.empty()) {
        payload = stringField(value, "reason");
    }
    if (!payload.empty() && !agentc::root1::setInlinePayload(descriptor, payload)) {
        error = "Root1 inline mailbox payload is too large";
        return false;
    }
    return true;
}

agentc::root1::ParticipantId participantFromValue(CPtr<agentc::ListreeValue> value) {
    std::string text;
    if (valueToString(value, text)) {
        uint64_t parsed = 0;
        if (parseU64(text, parsed)) {
            return static_cast<agentc::root1::ParticipantId>(parsed);
        }
    }

    uint64_t parsed = 0;
    if (u64Field(value, "participant_id", parsed) ||
        u64Field(value, "participant", parsed)) {
        return static_cast<agentc::root1::ParticipantId>(parsed);
    }

    auto waitable = namedValue(value, "waitable");
    if (u64Field(waitable, "participant_id", parsed)) {
        return static_cast<agentc::root1::ParticipantId>(parsed);
    }
    return 0;
}

CPtr<agentc::ListreeValue> waitableValue(agentc::root1::ParticipantId participant) {
    auto waitable = agentc::createNullValue();
    agentc::addNamedItem(waitable, "kind", agentc::createStringValue("root1-broker"));
    agentc::addNamedItem(waitable, "participant_id", agentc::createStringValue(std::to_string(participant)));
    return waitable;
}

uint64_t timeoutFromRequest(CPtr<agentc::ListreeValue> request) {
    std::string text;
    uint64_t parsed = 0;
    if (valueToString(request, text) && parseU64(text, parsed)) {
        return parsed;
    }
    if (u64Field(request, "timeout_ms", parsed)) {
        return parsed;
    }
    return 0;
}

uint64_t correlationFromRequest(CPtr<agentc::ListreeValue> request) {
    std::string text;
    uint64_t parsed = 0;
    if (valueToString(request, text) && parseU64(text, parsed)) {
        return parsed;
    }
    if (u64Field(request, "correlation_id", parsed) || u64Field(request, "sequence", parsed)) {
        return parsed;
    }
    return 0;
}

std::string reasonFromRequest(CPtr<agentc::ListreeValue> request) {
    std::string reason = stringField(request, "reason");
    if (reason.empty()) {
        reason = stringField(request, "payload");
    }
    return reason;
}

CPtr<agentc::ListreeValue> registerParticipant() {
    try {
        const auto participant = broker().registerParticipant();
        auto value = envelope("registered", true);
        agentc::addNamedItem(value, "participant_id", agentc::createStringValue(std::to_string(participant)));
        agentc::addNamedItem(value, "waitable", waitableValue(participant));
        return value;
    } catch (const std::exception& e) {
        return envelope("error", false, "root1_unavailable", e.what());
    }
}

CPtr<agentc::ListreeValue> poll(CPtr<agentc::ListreeValue> request) {
    try {
        const auto ready = broker().pollReadyParticipants(static_cast<int>(timeoutFromRequest(request)));
        auto participants = agentc::createListValue();
        for (const auto participant : ready) {
            agentc::addListItem(participants, agentc::createStringValue(std::to_string(participant)));
        }
        auto value = envelope("ready", true);
        agentc::addNamedItem(value, "participants", participants);
        return value;
    } catch (const std::exception& e) {
        return envelope("error", false, "root1_poll_failed", e.what());
    }
}

CPtr<agentc::ListreeValue> sendDescriptor(CPtr<agentc::ListreeValue> participantOrWaitable,
                                          CPtr<agentc::ListreeValue> descriptorValue) {
    try {
        const auto participant = participantFromValue(participantOrWaitable);
        if (participant == 0) {
            return envelope("error", false, "invalid_participant", "Root1 mailbox send requires a participant id or waitable");
        }
        agentc::root1::MailboxDescriptor descriptor;
        std::string error;
        if (!descriptorFromValue(descriptorValue, descriptor, error)) {
            return envelope("error", false, "invalid_descriptor", error);
        }
        if (!broker().sendMailboxDescriptor(participant, descriptor)) {
            return envelope("error", false, "send_failed", "Root1 mailbox send failed");
        }
        auto value = envelope("sent", true);
        agentc::addNamedItem(value, "participant_id", agentc::createStringValue(std::to_string(participant)));
        return value;
    } catch (const std::exception& e) {
        return envelope("error", false, "root1_send_failed", e.what());
    }
}

CPtr<agentc::ListreeValue> drainMailbox(CPtr<agentc::ListreeValue> participantOrWaitable) {
    try {
        const auto participant = participantFromValue(participantOrWaitable);
        if (participant == 0) {
            return agentc::createListValue();
        }
        broker().pollReadyParticipants(0);
        return descriptorsToList(broker().drainMailboxDescriptors(participant));
    } catch (...) {
        return agentc::createListValue();
    }
}

CPtr<agentc::ListreeValue> await(CPtr<agentc::ListreeValue> waitableOrRequest) {
    try {
        const auto participant = participantFromValue(waitableOrRequest);
        if (participant == 0) {
            return envelope("error", false, "invalid_waitable", "Root1 await requires a participant id or waitable");
        }
        broker().pollReadyParticipants(static_cast<int>(timeoutFromRequest(waitableOrRequest)));
        auto descriptors = broker().drainMailboxDescriptors(participant);
        auto value = envelope(descriptors.empty() ? "timeout" : "ready", true);
        agentc::addNamedItem(value, "participant_id", agentc::createStringValue(std::to_string(participant)));
        agentc::addNamedItem(value, "waitable", waitableValue(participant));
        agentc::addNamedItem(value, "events", descriptorsToList(descriptors));
        return value;
    } catch (const std::exception& e) {
        return envelope("error", false, "root1_await_failed", e.what());
    }
}

CPtr<agentc::ListreeValue> sendCancellation(CPtr<agentc::ListreeValue> participantOrWaitable,
                                            CPtr<agentc::ListreeValue> request) {
    try {
        const auto participant = participantFromValue(participantOrWaitable);
        if (participant == 0) {
            return envelope("error", false, "invalid_participant", "Root1 cancellation requires a participant id or waitable");
        }
        if (!broker().sendCancellation(participant, correlationFromRequest(request), reasonFromRequest(request))) {
            return envelope("error", false, "send_failed", "Root1 cancellation descriptor send failed");
        }
        auto value = envelope("sent", true);
        agentc::addNamedItem(value, "participant_id", agentc::createStringValue(std::to_string(participant)));
        agentc::addNamedItem(value, "kind", agentc::createStringValue("cancelled"));
        return value;
    } catch (const std::exception& e) {
        return envelope("error", false, "root1_cancel_failed", e.what());
    }
}

CPtr<agentc::ListreeValue> sendBackpressure(CPtr<agentc::ListreeValue> participantOrWaitable,
                                            CPtr<agentc::ListreeValue> request) {
    try {
        const auto participant = participantFromValue(participantOrWaitable);
        if (participant == 0) {
            return envelope("error", false, "invalid_participant", "Root1 backpressure requires a participant id or waitable");
        }
        if (!broker().sendBackpressure(participant, correlationFromRequest(request), reasonFromRequest(request))) {
            return envelope("error", false, "send_failed", "Root1 backpressure descriptor send failed");
        }
        auto value = envelope("sent", true);
        agentc::addNamedItem(value, "participant_id", agentc::createStringValue(std::to_string(participant)));
        agentc::addNamedItem(value, "kind", agentc::createStringValue("backpressure"));
        return value;
    } catch (const std::exception& e) {
        return envelope("error", false, "root1_backpressure_failed", e.what());
    }
}

} // namespace
} // namespace agentc::edict::root1

namespace {

LTV decode_ltv_handle(ltv value) {
    return LTV(static_cast<uint16_t>(value & 0xffffu),
               static_cast<uint16_t>((value >> 16) & 0xffffu));
}

ltv encode_ltv_handle(LTV value) {
    return static_cast<ltv>(static_cast<uint32_t>(value.first)
                            | (static_cast<uint32_t>(value.second) << 16));
}

CPtr<agentc::ListreeValue> borrow_ltv_value(ltv value) {
    if (value == 0) {
        return nullptr;
    }
    return agentc::ltv_borrow(decode_ltv_handle(value));
}

ltv release_ltv_value(CPtr<agentc::ListreeValue> value) {
    if (!value) {
        value = agentc::createNullValue();
    }
    return encode_ltv_handle(value.release());
}

} // namespace

extern "C" ltv agentc_root1_participant_register_ltv(void) {
    return release_ltv_value(agentc::edict::root1::registerParticipant());
}

extern "C" ltv agentc_root1_poll_ltv(ltv request) {
    return release_ltv_value(agentc::edict::root1::poll(borrow_ltv_value(request)));
}

extern "C" ltv agentc_root1_mailbox_send_ltv(ltv participant_or_waitable, ltv descriptor) {
    return release_ltv_value(agentc::edict::root1::sendDescriptor(borrow_ltv_value(participant_or_waitable),
                                                                 borrow_ltv_value(descriptor)));
}

extern "C" ltv agentc_root1_mailbox_drain_ltv(ltv participant_or_waitable) {
    return release_ltv_value(agentc::edict::root1::drainMailbox(borrow_ltv_value(participant_or_waitable)));
}

extern "C" ltv agentc_root1_await_ltv(ltv waitable_or_request) {
    return release_ltv_value(agentc::edict::root1::await(borrow_ltv_value(waitable_or_request)));
}

extern "C" ltv agentc_root1_send_cancellation_ltv(ltv participant_or_waitable, ltv request) {
    return release_ltv_value(agentc::edict::root1::sendCancellation(borrow_ltv_value(participant_or_waitable),
                                                                   borrow_ltv_value(request)));
}

extern "C" ltv agentc_root1_send_backpressure_ltv(ltv participant_or_waitable, ltv request) {
    return release_ltv_value(agentc::edict::root1::sendBackpressure(borrow_ltv_value(participant_or_waitable),
                                                                   borrow_ltv_value(request)));
}
