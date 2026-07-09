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

// G117 Phase 1 transitional bridge: AgentC-owned extern "C" façade over the
// DeltaGUI/GreekScope GEX analytics capability.
//
// Compiled directly against the read-only DeltaGUI sources (include path is
// injected by markethub/CMakeLists.txt from AGENTC_MARKETHUB_DELTAGUI_ROOT).
// No DeltaGUI file is modified. The AnalyticsService instance is process-local
// and non-durable; only JSON strings cross this boundary.

#include "greekscope_facade.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include "greekscope/analytics_service.h"

namespace {

AnalyticsService& service() {
    static AnalyticsService instance;
    return instance;
}

std::mutex& serviceMutex() {
    static std::mutex mutex;
    return mutex;
}

// Malloc-owned copy handed across the C ABI (freed by agentc_greekscope_free).
const char* copyForCaller(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) {
        return nullptr;
    }
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

std::string errorJson(const std::string& error) {
    json::Object obj;
    obj["ok"] = std::make_shared<json::Value>(false);
    obj["error"] = std::make_shared<json::Value>(error);
    return json::Value(obj).dump();
}

} // namespace

extern "C" const char* agentc_greekscope_facade_version(void) {
    return "agentc-greekscope-facade/1 gex";
}

extern "C" const char* agentc_greekscope_gex_seed_snapshot(const char* snapshot_json) {
    if (!snapshot_json) {
        return copyForCaller(errorJson("null_snapshot_json"));
    }
    std::lock_guard<std::mutex> lock(serviceMutex());
    std::string error;
    if (!service().seed_from_snapshot_json(snapshot_json, &error)) {
        return copyForCaller(errorJson(error.empty() ? "seed_failed" : error));
    }
    json::Object obj;
    obj["ok"] = std::make_shared<json::Value>(true);
    obj["status"] = std::make_shared<json::Value>(std::string("seeded"));
    return copyForCaller(json::Value(obj).dump());
}

extern "C" const char* agentc_greekscope_gex_snapshot(const char* symbol) {
    if (!symbol) {
        return copyForCaller(errorJson("null_symbol"));
    }
    std::lock_guard<std::mutex> lock(serviceMutex());
    std::string error;
    json::Object obj = service().gex_snapshot_json(symbol, &error);
    return copyForCaller(json::Value(obj).dump());
}

extern "C" int agentc_greekscope_gex_has_symbol(const char* symbol) {
    if (!symbol) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(serviceMutex());
    return service().has_symbol(symbol) ? 1 : 0;
}

extern "C" void agentc_greekscope_free(const char* text) {
    std::free(const_cast<char*>(text));
}
