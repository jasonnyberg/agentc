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

#include "edict_intern_service.h"
#include "agentc_worker_primitives.h"
#include "../cartographer/ltv_api.h"

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

extern "C" ltv agentc_worker_edict_active_count_ltv(void) {
    return release_ltv_value(agentc::edict::intern::activeCount());
}

extern "C" ltv agentc_worker_edict_prepare_task_ltv(ltv task) {
    return release_ltv_value(agentc::edict::intern::prepareTask(borrow_ltv_value(task), false));
}

extern "C" ltv agentc_worker_edict_capacity_status_ltv(ltv task_or_spec) {
    return release_ltv_value(agentc::edict::intern::capacityStatus(borrow_ltv_value(task_or_spec), false));
}

extern "C" ltv agentc_worker_edict_run_status_ltv(ltv task) {
    return release_ltv_value(agentc::edict::intern::runStatus(borrow_ltv_value(task), false));
}

extern "C" ltv agentc_worker_edict_run_status_prepared_ltv(ltv prepared_task) {
    return release_ltv_value(agentc::edict::intern::runStatusPrepared(borrow_ltv_value(prepared_task), false));
}

extern "C" ltv agentc_worker_edict_start_status_ltv(ltv task) {
    return release_ltv_value(agentc::edict::intern::startStatus(borrow_ltv_value(task), false));
}

extern "C" ltv agentc_worker_edict_start_status_prepared_ltv(ltv prepared_task) {
    return release_ltv_value(agentc::edict::intern::startStatusPrepared(borrow_ltv_value(prepared_task), false));
}

extern "C" ltv agentc_worker_edict_drain_events_ltv(ltv job_or_request) {
    return release_ltv_value(agentc::edict::intern::drainEvents(borrow_ltv_value(job_or_request)));
}

extern "C" ltv agentc_worker_edict_request_cancel_ltv(ltv job_or_request) {
    return release_ltv_value(agentc::edict::intern::requestCancel(borrow_ltv_value(job_or_request)));
}

extern "C" ltv agentc_worker_edict_collect_status_ltv(ltv job_or_request, ltv events) {
    return release_ltv_value(agentc::edict::intern::collectStatus(borrow_ltv_value(job_or_request),
                                                                 borrow_ltv_value(events)));
}

extern "C" ltv agentc_worker_edict_drop_ltv(ltv job_or_request) {
    return release_ltv_value(agentc::edict::intern::drop(borrow_ltv_value(job_or_request)));
}
