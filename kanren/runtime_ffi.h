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

#pragma once

#include <stdint.h>

#include "../cartographer/ltv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct agentc_runtime_ctx agentc_runtime_ctx;
typedef uint64_t agentc_value;

#define AGENTC_VALUE_NULL ((agentc_value)0)

agentc_runtime_ctx* agentc_runtime_create(void);
void agentc_runtime_destroy(agentc_runtime_ctx* ctx);

agentc_value agentc_runtime_value_from_ltv(agentc_runtime_ctx* ctx, LTV value);
LTV agentc_runtime_value_to_ltv(agentc_runtime_ctx* ctx, agentc_value value);

void agentc_runtime_value_retain(agentc_runtime_ctx* ctx, agentc_value value);
void agentc_runtime_value_release(agentc_runtime_ctx* ctx, agentc_value value);

agentc_value agentc_runtime_copy_last_error(agentc_runtime_ctx* ctx);
agentc_value agentc_logic_eval(agentc_runtime_ctx* ctx, agentc_value spec);
LTV agentc_logic_eval_ltv(LTV spec);

#ifdef __cplusplus
} // extern "C"
#endif
