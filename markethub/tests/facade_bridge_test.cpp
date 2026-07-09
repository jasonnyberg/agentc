// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

// G117 Phase 1 bridge acceptance: the AgentC-owned GreekScope façade is
// registered through the G116 symbol allowlist and invoked from generated C
// running under the TCC runtime (including isolated worker execution),
// returning structured JSON envelopes.
//
// This test is the one-way consumption boundary: markethub_tests links
// libedict to *use* AgentC facilities; core AgentC never links markethub.

#include <gtest/gtest.h>

#include <dlfcn.h>

#include <filesystem>
#include <string>

#include "edict/tcc_runtime.h"

#ifndef MARKETHUB_HAVE_TCC
#define MARKETHUB_HAVE_TCC 0
#endif

#ifndef MARKETHUB_HAVE_FACADE
#define MARKETHUB_HAVE_FACADE 0
#endif

#ifndef MARKETHUB_FACADE_LIBRARY
#define MARKETHUB_FACADE_LIBRARY ""
#endif

using agentc::edict::tcc::TccCompilerService;

namespace {

constexpr const char* kVersionDecl =
    "const char* agentc_greekscope_facade_version(void);";
constexpr const char* kSeedDecl =
    "const char* agentc_greekscope_gex_seed_snapshot(const char* snapshot_json);";
constexpr const char* kSnapshotDecl =
    "const char* agentc_greekscope_gex_snapshot(const char* symbol);";
constexpr const char* kHasSymbolDecl =
    "int agentc_greekscope_gex_has_symbol(const char* symbol);";
constexpr const char* kFreeDecl =
    "void agentc_greekscope_free(const char* text);";

// Generated-C adapter: seeds the GEX book from the snapshot JSON passed as
// arg 0, then returns the derived GEX snapshot JSON for the symbol in arg 1.
constexpr const char* kGexAdapterSource = R"(
int agentc_tcc_entry(agentc_tcc_call* call) {
    const char* snapshot_json = agentc_tcc_arg_text(call, 0);
    const char* symbol = agentc_tcc_arg_text(call, 1);

    const char* seeded = agentc_greekscope_gex_seed_snapshot(snapshot_json);
    if (!seeded) {
        agentc_tcc_result_text(call, "{\"ok\":false,\"error\":\"seed_null\"}");
        return 1;
    }
    agentc_tcc_log(call, seeded);
    agentc_greekscope_free(seeded);

    if (!agentc_greekscope_gex_has_symbol(symbol)) {
        agentc_tcc_result_text(call, "{\"ok\":false,\"error\":\"symbol_not_seeded\"}");
        return 2;
    }

    const char* snapshot = agentc_greekscope_gex_snapshot(symbol);
    if (!snapshot) {
        agentc_tcc_result_text(call, "{\"ok\":false,\"error\":\"snapshot_null\"}");
        return 3;
    }
    agentc_tcc_result_text(call, snapshot);
    agentc_greekscope_free(snapshot);
    return 0;
}
)";

constexpr const char* kVersionAdapterSource = R"(
int agentc_tcc_entry(agentc_tcc_call* call) {
    agentc_tcc_result_text(call, agentc_greekscope_facade_version());
    return 0;
}
)";

// Explicitly declares the façade symbol so compilation succeeds and the
// failure surfaces at relocation (mirrors kUnauthorizedSymbolSource in
// tcc_runtime_test.cpp).
constexpr const char* kUnauthorizedFacadeSource = R"(
extern const char* agentc_greekscope_facade_version(void);
int agentc_tcc_entry(agentc_tcc_call* call) {
    agentc_tcc_result_text(call, agentc_greekscope_facade_version());
    return 0;
}
)";

// Minimal fixture option-chain snapshot (AnalyticsService seed schema).
constexpr const char* kFixtureSnapshotJson = R"({
  "underlying": "AMD",
  "underlying_price": 120.0,
  "risk_free_rate": 0.05,
  "timestamp": "2026-07-07T12:00:00Z",
  "provider": "fixture",
  "contracts": [
    {"symbol": "AMD_2506C125", "underlying": "AMD", "expiration": "2026-08-21",
     "option_type": "CALL", "strike": 125.0, "days_to_expiration": 45,
     "implied_volatility": 0.32, "mark": 4.10, "bid": 4.00, "ask": 4.20,
     "last": 4.05, "open_interest": 1500, "volume": 320},
    {"symbol": "AMD_2506P115", "underlying": "AMD", "expiration": "2026-08-21",
     "option_type": "PUT", "strike": 115.0, "days_to_expiration": 45,
     "implied_volatility": 0.35, "mark": 3.60, "bid": 3.50, "ask": 3.70,
     "last": 3.55, "open_interest": 1200, "volume": 210}
  ]
})";

std::string facadeLibraryPath() {
    return std::filesystem::path(MARKETHUB_FACADE_LIBRARY).string();
}

// GTEST_SKIP only returns from the enclosing function, so bridge guards must
// expand at test scope (a helper function would skip the helper, then the
// test body would keep running — observed in the facade-disabled build).
#define REQUIRE_BRIDGE(service)                                              \
    do {                                                                     \
        if (!MARKETHUB_HAVE_TCC) {                                           \
            GTEST_SKIP() << "libtcc support not enabled in this build";      \
        }                                                                    \
        if (!MARKETHUB_HAVE_FACADE) {                                        \
            GTEST_SKIP() << "GreekScope facade not built (DeltaGUI absent)"; \
        }                                                                    \
        const auto availability_ = (service).availability();                 \
        if (!availability_.available) {                                      \
            GTEST_SKIP() << "TCC runtime unavailable: " << availability_.error; \
        }                                                                    \
        ASSERT_TRUE(std::filesystem::exists(facadeLibraryPath()))            \
            << facadeLibraryPath();                                          \
    } while (0)

void allowFacadeSymbols(TccCompilerService& service) {
    const std::string lib = facadeLibraryPath();
    struct { const char* name; const char* decl; } symbols[] = {
        {"agentc_greekscope_facade_version", kVersionDecl},
        {"agentc_greekscope_gex_seed_snapshot", kSeedDecl},
        {"agentc_greekscope_gex_snapshot", kSnapshotDecl},
        {"agentc_greekscope_gex_has_symbol", kHasSymbolDecl},
        {"agentc_greekscope_free", kFreeDecl},
    };
    for (const auto& symbol : symbols) {
        auto allowed = service.allowLibrarySymbol(lib, symbol.name, symbol.decl);
        ASSERT_TRUE(allowed.ok) << symbol.name << ": " << allowed.error;
    }
}

} // namespace

// Direct dlopen sanity check on the façade contract itself (no TCC needed).
TEST(GreekscopeFacadeTest, FacadeExportsWorkThroughPlainDlopen) {
    if (!MARKETHUB_HAVE_FACADE) {
        GTEST_SKIP() << "GreekScope facade not built (DeltaGUI tree absent)";
    }
    void* handle = dlopen(facadeLibraryPath().c_str(), RTLD_NOW | RTLD_LOCAL);
    ASSERT_NE(handle, nullptr) << dlerror();

    using VersionFn = const char* (*)();
    using SeedFn = const char* (*)(const char*);
    using SnapshotFn = const char* (*)(const char*);
    using HasFn = int (*)(const char*);
    using FreeFn = void (*)(const char*);

    auto version = reinterpret_cast<VersionFn>(dlsym(handle, "agentc_greekscope_facade_version"));
    auto seed = reinterpret_cast<SeedFn>(dlsym(handle, "agentc_greekscope_gex_seed_snapshot"));
    auto snapshot = reinterpret_cast<SnapshotFn>(dlsym(handle, "agentc_greekscope_gex_snapshot"));
    auto hasSymbol = reinterpret_cast<HasFn>(dlsym(handle, "agentc_greekscope_gex_has_symbol"));
    auto release = reinterpret_cast<FreeFn>(dlsym(handle, "agentc_greekscope_free"));
    ASSERT_TRUE(version && seed && snapshot && hasSymbol && release);

    EXPECT_NE(std::string(version()).find("agentc-greekscope-facade"), std::string::npos);

    const char* seeded = seed(kFixtureSnapshotJson);
    ASSERT_NE(seeded, nullptr);
    EXPECT_NE(std::string(seeded).find("\"ok\":true"), std::string::npos);
    release(seeded);

    EXPECT_EQ(hasSymbol("AMD"), 1);
    EXPECT_EQ(hasSymbol("MSFT"), 0);

    const char* snap = snapshot("AMD");
    ASSERT_NE(snap, nullptr);
    const std::string snapJson(snap);
    release(snap);
    EXPECT_NE(snapJson.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(snapJson.find("\"symbol\":\"AMD\""), std::string::npos);
    EXPECT_NE(snapJson.find("net_gex"), std::string::npos);

    // Unseeded symbol returns a structured error envelope, not a crash.
    const char* missing = snapshot("MSFT");
    ASSERT_NE(missing, nullptr);
    EXPECT_NE(std::string(missing).find("symbol_not_seeded"), std::string::npos);
    release(missing);

    dlclose(handle);
}

// Phase 1 bridge AC: allowlisted façade symbols invoked from generated C
// through the TCC runtime (in-process run path).
TEST(GreekscopeFacadeBridgeTest, TccAdapterCallsFacadeThroughAllowlist) {
    TccCompilerService service;
    REQUIRE_BRIDGE(service);
    allowFacadeSymbols(service);

    auto compiled = service.compile(kVersionAdapterSource);
    ASSERT_TRUE(compiled.ok) << compiled.error;

    auto run = service.run(compiled.moduleId, {});
    ASSERT_TRUE(run.ok) << run.error;
    EXPECT_EQ(run.resultKind, "text");
    EXPECT_NE(run.resultText.find("agentc-greekscope-facade"), std::string::npos);
}

// Phase 1 bridge AC (isolated form): the same adapter pattern executes in an
// isolated TCC worker process and returns a structured JSON envelope with
// derived GEX analytics for fixture data.
TEST(GreekscopeFacadeBridgeTest, IsolatedTccWorkerReturnsGexSnapshotJson) {
    TccCompilerService service;
    REQUIRE_BRIDGE(service);
    allowFacadeSymbols(service);

    auto compiled = service.compile(kGexAdapterSource);
    ASSERT_TRUE(compiled.ok) << compiled.error;

    auto started = service.startIsolated(
        compiled.moduleId, {kFixtureSnapshotJson, "AMD"}, 10000);
    ASSERT_TRUE(started.ok) << started.error;
    EXPECT_EQ(started.launchMode, "exec");

    auto collected = service.collect(started.jobId);
    ASSERT_TRUE(collected.ok) << collected.error << " status=" << collected.status;
    EXPECT_EQ(collected.resultKind, "text");

    // Structured envelope from the façade, produced inside the worker.
    EXPECT_NE(collected.resultText.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(collected.resultText.find("\"symbol\":\"AMD\""), std::string::npos);
    EXPECT_NE(collected.resultText.find("net_gex"), std::string::npos);
    EXPECT_NE(collected.resultText.find("gamma_flip_price"), std::string::npos);

    // The seed acknowledgement travelled back through the log channel.
    ASSERT_FALSE(collected.logs.empty());
    EXPECT_NE(collected.logs.front().find("\"ok\":true"), std::string::npos);

    // Unauthorized symbols remain rejected: generated C referencing a symbol
    // outside the allowlist must fail to relocate (safety regression guard).
    service.clearAllowedSymbols();
    auto rejected = service.compile(kUnauthorizedFacadeSource);
    EXPECT_FALSE(rejected.ok);
    EXPECT_EQ(rejected.status, "relocate_error");
}
