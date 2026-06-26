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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct agentc_tcc_call agentc_tcc_call;

int agentc_tcc_arg_count(agentc_tcc_call* call);
const char* agentc_tcc_arg_text(agentc_tcc_call* call, int index);
long long agentc_tcc_parse_i64(const char* text);
double agentc_tcc_parse_f64(const char* text);
void agentc_tcc_result_text(agentc_tcc_call* call, const char* text);
void agentc_tcc_result_i64(agentc_tcc_call* call, long long value);
void agentc_tcc_result_f64(agentc_tcc_call* call, double value);
void agentc_tcc_log(agentc_tcc_call* call, const char* message);

#ifdef __cplusplus
}
#endif
