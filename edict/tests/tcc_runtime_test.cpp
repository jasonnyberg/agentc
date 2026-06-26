// This file is part of AgentC.
//
// AgentC is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "../edict_vm.h"
#include "../tcc_runtime.h"

#ifndef TEST_TCC_ADAPTER_PATH
#define TEST_TCC_ADAPTER_PATH ""
#endif

#ifndef AGENTC_HAVE_TCC
#define AGENTC_HAVE_TCC 0
#endif

using agentc::ListreeValue;
using agentc::edict::EdictCompiler;
using agentc::edict::EdictVM;
using agentc::edict::tcc::TccCompilerService;
using namespace agentc::edict;

namespace {

constexpr const char* kScaleDecl = "long long agentc_tcc_test_scale(long long value);";
constexpr const char* kProviderDecl = "const char* agentc_tcc_test_provider_name(void);";
constexpr const char* kScaleSource = R"(
int agentc_tcc_entry(agentc_tcc_call* call) {
    long long value = agentc_tcc_parse_i64(agentc_tcc_arg_text(call, 0));
    agentc_tcc_result_i64(call, agentc_tcc_test_scale(value));
    return 0;
}
)";
constexpr const char* kProviderSource = R"(
int agentc_tcc_entry(agentc_tcc_call* call) {
    agentc_tcc_result_text(call, agentc_tcc_test_provider_name());
    return 0;
}
)";
constexpr const char* kProviderSourceInline =
    "int agentc_tcc_entry(agentc_tcc_call* call){agentc_tcc_result_text(call, agentc_tcc_test_provider_name());return 0;}";
constexpr const char* kSpinSource = R"(
int agentc_tcc_entry(agentc_tcc_call* call) {
    (void)call;
    for (;;) {
    }
    return 0;
}
)";
constexpr const char* kEntryErrorSource = R"(
int agentc_tcc_entry(agentc_tcc_call* call) {
    agentc_tcc_log(call, "failing on purpose");
    return 7;
}
)";
constexpr const char* kCrashSource = R"(
int agentc_tcc_entry(agentc_tcc_call* call) {
    (void)call;
    *(volatile int*)0 = 1;
    return 0;
}
)";
constexpr const char* kCompileErrorSource = R"(
int agentc_tcc_entry(agentc_tcc_call* call) {
    agentc_tcc_result_i64(call, 7)
    return 0;
}
)";
constexpr const char* kMissingEntrySource = R"(
int some_other_entry(agentc_tcc_call* call) {
    agentc_tcc_result_i64(call, 7);
    return 0;
}
)";
constexpr const char* kUnauthorizedSymbolSource = R"(
extern long long agentc_tcc_test_scale(long long value);
int agentc_tcc_entry(agentc_tcc_call* call) {
    agentc_tcc_result_i64(call, agentc_tcc_test_scale(7));
    return 0;
}
)";

std::string adapterLibraryPath() {
    return std::filesystem::path(TEST_TCC_ADAPTER_PATH).string();
}

void requireTccBuild() {
    if (!AGENTC_HAVE_TCC) {
        GTEST_SKIP() << "libtcc support not enabled in this build";
    }
}

void requireAvailable(TccCompilerService& service) {
    requireTccBuild();
    const auto availability = service.availability();
    ASSERT_TRUE(availability.available) << availability.error;
    ASSERT_TRUE(std::filesystem::exists(adapterLibraryPath())) << adapterLibraryPath();
}

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

} // namespace

TEST(TccRuntimeTest, CompileRunListSymbolsAndDropUsingLibraryAllowlist) {
    TccCompilerService service;
    requireAvailable(service);

    auto allowedScale = service.allowLibrarySymbol(adapterLibraryPath(),
                                                   "agentc_tcc_test_scale",
                                                   kScaleDecl);
    ASSERT_TRUE(allowedScale.ok) << allowedScale.error;
    auto allowedProvider = service.allowLibrarySymbol(adapterLibraryPath(),
                                                      "agentc_tcc_test_provider_name",
                                                      kProviderDecl);
    ASSERT_TRUE(allowedProvider.ok) << allowedProvider.error;

    auto compiled = service.compile(kProviderSource);
    ASSERT_TRUE(compiled.ok) << compiled.error;
    ASSERT_FALSE(compiled.moduleId.empty());
    EXPECT_EQ(compiled.status, "compiled");

    auto symbols = service.listSymbols(compiled.moduleId);
    ASSERT_TRUE(symbols.ok) << symbols.error;
    EXPECT_NE(std::find(symbols.symbols.begin(), symbols.symbols.end(), "agentc_tcc_entry"),
              symbols.symbols.end());

    auto run = service.run(compiled.moduleId, {});
    ASSERT_TRUE(run.ok) << run.error;
    EXPECT_EQ(run.resultKind, "text");
    EXPECT_EQ(run.resultText, "agentc-deltagui-adapter-probe");

    auto dropped = service.drop(compiled.moduleId);
    ASSERT_TRUE(dropped.ok) << dropped.error;
    EXPECT_EQ(dropped.status, "dropped");
}

TEST(TccRuntimeTest, IsolatedRunCollectsResultThroughWorkerExec) {
    TccCompilerService service;
    requireAvailable(service);

    auto allowedScale = service.allowLibrarySymbol(adapterLibraryPath(),
                                                   "agentc_tcc_test_scale",
                                                   kScaleDecl);
    ASSERT_TRUE(allowedScale.ok) << allowedScale.error;

    auto compiled = service.compile(kScaleSource);
    ASSERT_TRUE(compiled.ok) << compiled.error;

    auto started = service.startIsolated(compiled.moduleId, {"5"}, 2000);
    ASSERT_TRUE(started.ok) << started.error;
    EXPECT_EQ(started.status, "running");
    EXPECT_EQ(started.launchMode, "exec");

    auto collected = service.collect(started.jobId);
    ASSERT_TRUE(collected.ok) << collected.error;
    EXPECT_EQ(collected.resultKind, "i64");
    EXPECT_EQ(collected.resultI64, 22);
    EXPECT_EQ(collected.launchMode, "exec");
}

TEST(TccRuntimeTest, IsolatedRunTimesOutAndReturnsFailureEnvelope) {
    TccCompilerService service;
    requireAvailable(service);

    auto compiled = service.compile(kSpinSource);
    ASSERT_TRUE(compiled.ok) << compiled.error;

    auto started = service.startIsolated(compiled.moduleId, {}, 50);
    ASSERT_TRUE(started.ok) << started.error;

    auto collected = service.collect(started.jobId);
    EXPECT_FALSE(collected.ok);
    EXPECT_EQ(collected.status, "timed_out");
    EXPECT_EQ(collected.launchMode, "exec");
}

TEST(TccRuntimeTest, IsolatedRunCancelReturnsCancellationEnvelope) {
    TccCompilerService service;
    requireAvailable(service);

    auto compiled = service.compile(kSpinSource);
    ASSERT_TRUE(compiled.ok) << compiled.error;

    auto started = service.startIsolated(compiled.moduleId, {}, 5000);
    ASSERT_TRUE(started.ok) << started.error;
    EXPECT_EQ(started.status, "running");

    auto cancelled = service.cancel(started.jobId);
    EXPECT_FALSE(cancelled.ok);
    EXPECT_EQ(cancelled.status, "cancelled");
    EXPECT_EQ(cancelled.launchMode, "exec");

    auto collected = service.collect(started.jobId);
    EXPECT_FALSE(collected.ok);
    EXPECT_EQ(collected.status, "cancelled");
}

TEST(TccRuntimeTest, IsolatedRunCrashReturnsSignalEnvelope) {
    TccCompilerService service;
    requireAvailable(service);

    auto compiled = service.compile(kCrashSource);
    ASSERT_TRUE(compiled.ok) << compiled.error;

    auto started = service.startIsolated(compiled.moduleId, {}, 2000);
    ASSERT_TRUE(started.ok) << started.error;

    auto collected = service.collect(started.jobId);
    EXPECT_FALSE(collected.ok);
    EXPECT_EQ(collected.status, "runtime_signal");
    EXPECT_NE(collected.signalNumber, 0);
}

TEST(TccRuntimeTest, IsolatedRunNonZeroEntryReturnsRuntimeFailureEnvelope) {
    TccCompilerService service;
    requireAvailable(service);

    auto compiled = service.compile(kEntryErrorSource);
    ASSERT_TRUE(compiled.ok) << compiled.error;

    auto started = service.startIsolated(compiled.moduleId, {}, 2000);
    ASSERT_TRUE(started.ok) << started.error;

    auto collected = service.collect(started.jobId);
    EXPECT_FALSE(collected.ok);
    EXPECT_EQ(collected.status, "entry_error");
    EXPECT_EQ(collected.exitCode, 7);
    ASSERT_EQ(collected.logs.size(), 1u);
    EXPECT_EQ(collected.logs.front(), "failing on purpose");
}

TEST(TccRuntimeTest, CompileMalformedSourceReturnsCompileErrorEnvelope) {
    TccCompilerService service;
    requireAvailable(service);

    auto compiled = service.compile(kCompileErrorSource);
    EXPECT_FALSE(compiled.ok);
    EXPECT_EQ(compiled.status, "compile_error");
    EXPECT_FALSE(compiled.diagnostics.empty());
}

TEST(TccRuntimeTest, CompileWithoutEntryReturnsMissingEntryEnvelope) {
    TccCompilerService service;
    requireAvailable(service);

    auto compiled = service.compile(kMissingEntrySource);
    EXPECT_FALSE(compiled.ok);
    EXPECT_EQ(compiled.status, "missing_entry_symbol");
    EXPECT_NE(std::find(compiled.symbols.begin(), compiled.symbols.end(), "some_other_entry"),
              compiled.symbols.end());
}

TEST(TccRuntimeTest, CompileWithoutAllowedSymbolReturnsRelocateError) {
    TccCompilerService service;
    requireAvailable(service);

    auto compiled = service.compile(kUnauthorizedSymbolSource);
    EXPECT_FALSE(compiled.ok);
    EXPECT_EQ(compiled.status, "relocate_error");
    EXPECT_FALSE(compiled.diagnostics.empty());
}

TEST(TccRuntimeTest, EdictBuiltinsExposeCompileRunAndAllowlistSurface) {
    TccCompilerService probe;
    requireAvailable(probe);

    EdictVM vm(agentc::createNullValue());

    BytecodeBuffer availabilityCode;
    availabilityCode.addOp(VMOP_TCC_AVAILABLE);
    ASSERT_FALSE(vm.execute(availabilityCode) & VM_ERROR) << vm.getError();
    auto availability = vm.popData();
    ASSERT_TRUE(availability);
    EXPECT_EQ(textValue(namedValue(availability, "status")), "available");
    EXPECT_EQ(textValue(namedValue(availability, "available")), "true");

    vm.pushData(agentc::createStringValue(adapterLibraryPath()));
    vm.pushData(agentc::createStringValue("agentc_tcc_test_provider_name"));
    vm.pushData(agentc::createStringValue(kProviderDecl));
    BytecodeBuffer allowCode;
    allowCode.addOp(VMOP_TCC_ALLOW_LIBRARY_SYMBOL);
    ASSERT_FALSE(vm.execute(allowCode) & VM_ERROR) << vm.getError();
    auto allowed = vm.popData();
    ASSERT_TRUE(allowed);
    EXPECT_EQ(textValue(namedValue(allowed, "status")), "allowed");
    EXPECT_EQ(textValue(namedValue(allowed, "handle_kind")), "tcc_symbol");

    vm.pushData(agentc::createStringValue(kProviderSourceInline));
    BytecodeBuffer compileCode;
    compileCode.addOp(VMOP_TCC_COMPILE);
    ASSERT_FALSE(vm.execute(compileCode) & VM_ERROR) << vm.getError();
    auto module = vm.popData();
    ASSERT_TRUE(module);
    EXPECT_EQ(textValue(namedValue(module, "status")), "compiled");
    EXPECT_EQ(textValue(namedValue(module, "handle_kind")), "tcc_module");

    auto args = agentc::createListValue();
    agentc::addListItem(args, agentc::createStringValue("ignored"));
    vm.pushData(module);
    vm.pushData(args);
    BytecodeBuffer runCode;
    runCode.addOp(VMOP_TCC_RUN);
    ASSERT_FALSE(vm.execute(runCode) & VM_ERROR) << vm.getError();
    auto result = vm.popData();
    ASSERT_TRUE(result);
    EXPECT_EQ(textValue(namedValue(result, "status")), "ok");
    EXPECT_EQ(textValue(namedValue(result, "result_kind")), "text");
    EXPECT_EQ(textValue(namedValue(result, "result_text")), "agentc-deltagui-adapter-probe");
}

TEST(TccRuntimeTest, EdictBuiltinsCompileErrorReturnsEnvelope) {
    TccCompilerService probe;
    requireAvailable(probe);

    EdictVM vm(agentc::createNullValue());
    vm.pushData(agentc::createStringValue(kCompileErrorSource));

    BytecodeBuffer compileCode;
    compileCode.addOp(VMOP_TCC_COMPILE);
    ASSERT_FALSE(vm.execute(compileCode) & VM_ERROR) << vm.getError();

    auto envelope = vm.popData();
    ASSERT_TRUE(envelope);
    EXPECT_EQ(textValue(namedValue(envelope, "status")), "compile_error");
    EXPECT_FALSE(textValue(namedValue(envelope, "error")).empty());
    EXPECT_TRUE(namedValue(envelope, "diagnostics"));
}

TEST(TccRuntimeTest, EdictBuiltinsSymbolsAndDropCoverLifecycle) {
    TccCompilerService probe;
    requireAvailable(probe);

    EdictVM vm(agentc::createNullValue());

    vm.pushData(agentc::createStringValue(adapterLibraryPath()));
    vm.pushData(agentc::createStringValue("agentc_tcc_test_provider_name"));
    vm.pushData(agentc::createStringValue(kProviderDecl));
    BytecodeBuffer allowCode;
    allowCode.addOp(VMOP_TCC_ALLOW_LIBRARY_SYMBOL);
    ASSERT_FALSE(vm.execute(allowCode) & VM_ERROR) << vm.getError();
    ASSERT_TRUE(vm.popData());

    vm.pushData(agentc::createStringValue(kProviderSourceInline));
    BytecodeBuffer compileCode;
    compileCode.addOp(VMOP_TCC_COMPILE);
    ASSERT_FALSE(vm.execute(compileCode) & VM_ERROR) << vm.getError();
    auto module = vm.popData();
    ASSERT_TRUE(module);
    EXPECT_EQ(textValue(namedValue(module, "status")), "compiled");

    vm.pushData(module);
    BytecodeBuffer symbolsCode;
    symbolsCode.addOp(VMOP_TCC_SYMBOLS);
    ASSERT_FALSE(vm.execute(symbolsCode) & VM_ERROR) << vm.getError();
    auto symbols = vm.popData();
    ASSERT_TRUE(symbols);
    EXPECT_EQ(textValue(namedValue(symbols, "status")), "symbols");
    ASSERT_TRUE(namedValue(symbols, "symbols"));

    vm.pushData(module);
    BytecodeBuffer dropCode;
    dropCode.addOp(VMOP_TCC_DROP);
    ASSERT_FALSE(vm.execute(dropCode) & VM_ERROR) << vm.getError();
    auto dropped = vm.popData();
    ASSERT_TRUE(dropped);
    EXPECT_EQ(textValue(namedValue(dropped, "status")), "dropped");

    auto args = agentc::createListValue();
    vm.pushData(module);
    vm.pushData(args);
    BytecodeBuffer runCode;
    runCode.addOp(VMOP_TCC_RUN);
    ASSERT_FALSE(vm.execute(runCode) & VM_ERROR) << vm.getError();
    auto missingModule = vm.popData();
    ASSERT_TRUE(missingModule);
    EXPECT_EQ(textValue(namedValue(missingModule, "status")), "module_not_found");
}
