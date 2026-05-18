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

#ifndef AGENTC_WORKER_PRIMITIVES_H
#define AGENTC_WORKER_PRIMITIVES_H

#ifdef __cplusplus
extern "C" {
#endif

// Cartographer FFI LTV passthrough handle.  This mirrors the lightweight
// C-domain convention used by extensions/agentc_stdlib.h so this header is
// easy for the resolver to parse as an importable primitive surface.
typedef unsigned int ltv;

// Transitional G111 worker primitives.  These expose the current Edict-worker
// backend through the FFI path so public intern words can migrate out of VM
// opcode dispatch.  They intentionally traffic in LTV values rather than raw
// fds/pointers; future Root1/worker primitives should split out more generic
// participant, waitable, mailbox, and envelope-construction policy.
ltv agentc_worker_edict_active_count_ltv(void);
ltv agentc_worker_edict_prepare_task_ltv(ltv task);
ltv agentc_worker_edict_check_capacity_ltv(ltv task_or_spec);
ltv agentc_worker_edict_capacity_status_ltv(ltv task_or_spec);
ltv agentc_worker_edict_run_ltv(ltv task);
ltv agentc_worker_edict_run_prepared_ltv(ltv prepared_task);
ltv agentc_worker_edict_start_ltv(ltv task);
ltv agentc_worker_edict_start_prepared_ltv(ltv prepared_task);
ltv agentc_worker_edict_drain_events_ltv(ltv job_or_request);
ltv agentc_worker_edict_request_cancel_ltv(ltv job_or_request);
ltv agentc_worker_edict_collect_ltv(ltv job_or_request, ltv events);
ltv agentc_worker_edict_collect_status_ltv(ltv job_or_request, ltv events);
ltv agentc_worker_edict_drop_ltv(ltv job_or_request);
ltv agentc_worker_edict_sync_ltv(ltv job_or_request);
ltv agentc_worker_edict_cancel_ltv(ltv job_or_request);

#ifdef __cplusplus
}
#endif

#endif // AGENTC_WORKER_PRIMITIVES_H
