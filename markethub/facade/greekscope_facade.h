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

// G117 Phase 1 — AgentC-owned extern "C" façade over the DeltaGUI/GreekScope
// GEX analytics capability (anti-corruption boundary, WP_G117 §6).
//
// Contracts are JSON/string/scalar only. C++ types, provider handles, and
// credentials never cross this boundary. Every returned const char* is
// malloc-owned by the caller and must be released via agentc_greekscope_free.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Static façade identity/version string (do NOT free).
const char* agentc_greekscope_facade_version(void);

// Seeds the process-local GEX book from an option-chain snapshot JSON
// (same schema as AnalyticsService::seed_from_snapshot_json).
// Returns malloc-owned JSON: {"ok":true,"symbol":...} or {"ok":false,"error":...}.
const char* agentc_greekscope_gex_seed_snapshot(const char* snapshot_json);

// Returns the malloc-owned GEX snapshot JSON for a seeded symbol
// (net GEX, gamma flip, call/put walls, expiry summaries, quality flags).
const char* agentc_greekscope_gex_snapshot(const char* symbol);

// 1 when the symbol has been seeded, else 0.
int agentc_greekscope_gex_has_symbol(const char* symbol);

// Releases any malloc-owned string returned by this façade.
void agentc_greekscope_free(const char* text);

#ifdef __cplusplus
}
#endif
