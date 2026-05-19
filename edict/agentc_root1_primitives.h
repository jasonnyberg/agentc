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

#ifndef AGENTC_ROOT1_PRIMITIVES_H
#define AGENTC_ROOT1_PRIMITIVES_H

#ifdef __cplusplus
extern "C" {
#endif

// Cartographer FFI LTV passthrough handle.  Root1 primitives expose logical
// participant/waitable/descriptor values, never durable raw fds.
typedef unsigned int ltv;

ltv agentc_root1_resource_create_ltv(ltv request);
ltv agentc_root1_participant_register_ltv(void);
ltv agentc_root1_poll_ltv(ltv request);
ltv agentc_root1_mailbox_send_ltv(ltv participant_or_waitable, ltv descriptor);
ltv agentc_root1_mailbox_drain_ltv(ltv participant_or_waitable);
ltv agentc_root1_resource_acquire_ltv(ltv participant_or_request, ltv resource_or_request);
ltv agentc_root1_resource_release_ltv(ltv participant_or_request, ltv resource_or_request);
ltv agentc_root1_lease_register_ltv(ltv participant_or_request, ltv resource_or_request, ltv request);
ltv agentc_root1_heartbeat_ltv(ltv participant_or_request, ltv request);
ltv agentc_root1_recover_expired_ltv(ltv request);
ltv agentc_root1_await_ltv(ltv waitable_or_request);
ltv agentc_root1_send_cancellation_ltv(ltv participant_or_waitable, ltv request);
ltv agentc_root1_send_backpressure_ltv(ltv participant_or_waitable, ltv request);

#ifdef __cplusplus
}
#endif

#endif // AGENTC_ROOT1_PRIMITIVES_H
