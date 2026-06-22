// G093 acceptance-criteria application: simulated worker scenario using
// overlay dictionaries for reference-scoped ReadOnly sharing.
//
// A coordinator creates a frozen shared configuration, spawns a worker with
// an overlay that shadows specific keys, the worker reads effective values
// (shadow > shared), and the coordinator inspects the worker's diff without
// the frozen shared base being modified.

#include <iostream>
#include <string>
#include <vector>

#include "../edict/edict_compiler.h"
#include "../edict/edict_vm.h"

using namespace agentc::edict;

namespace {

CPtr<agentc::ListreeValue> namedValue(CPtr<agentc::ListreeValue> value, const std::string& name) {
    if (!value || value->isListMode()) return nullptr;
    auto item = value->find(name);
    return item ? item->getValue(false, false) : nullptr;
}

std::string textValue(CPtr<agentc::ListreeValue> value) {
    if (!value || !value->getData()) return {};
    return std::string(static_cast<const char*>(value->getData()), value->getLength());
}

std::vector<std::string> listStrings(CPtr<agentc::ListreeValue> value) {
    std::vector<std::string> out;
    if (!value || !value->isListMode()) return out;
    value->forEachList([&](CPtr<agentc::ListreeValueRef>& ref) {
        if (ref && ref->getValue() && ref->getValue()->getData()) {
            out.emplace_back(static_cast<const char*>(ref->getValue()->getData()), ref->getValue()->getLength());
        }
    });
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace

int main() {
    EdictVM vm;
    EdictCompiler compiler;

    // Phase 1: Coordinator creates frozen shared configuration
    int state = vm.execute(compiler.compile(R"(
        {"model": "gpt-4", "temperature": "0.7", "max_tokens": "2048", "system_prompt": "You are a helpful assistant."} @config
        config freeze! @shared_config
    )"));
    if (state & VM_ERROR) { std::cerr << vm.getError() << "\n"; return 1; }

    // Phase 2: Worker creates an overlay and shadows specific keys
    state = vm.execute(compiler.compile(R"(
        shared_config overlay.new! @worker_overlay
        worker_overlay "temperature" "0.2" overlay.set! @worker_overlay
        worker_overlay "max_tokens" "512" overlay.set! @worker_overlay
        worker_overlay "custom_instruction" "Focus on code review." overlay.set! @worker_overlay
    )"));
    if (state & VM_ERROR) { std::cerr << vm.getError() << "\n"; return 1; }

    // Phase 3: Worker reads effective values (shadow > shared)
    state = vm.execute(compiler.compile(R"(
        worker_overlay "temperature" overlay.get! @worker_temperature
        worker_overlay "max_tokens" overlay.get! @worker_max_tokens
        worker_overlay "model" overlay.get! @worker_model
        worker_overlay "system_prompt" overlay.get! @worker_system_prompt
        worker_overlay "custom_instruction" overlay.get! @worker_custom_instruction
    )"));
    if (state & VM_ERROR) { std::cerr << vm.getError() << "\n"; return 1; }

    // Phase 4: Coordinator inspects the worker's diff
    state = vm.execute(compiler.compile(R"(
        worker_overlay overlay.commit! @worker_diff
        worker_overlay overlay.shadow_keys! @worker_shadow_keys
        shared_config.temperature @coordinator_temperature
        shared_config.max_tokens @coordinator_max_tokens
    )"));
    if (state & VM_ERROR) { std::cerr << vm.getError() << "\n"; return 1; }

    auto root = vm.getCursor().getValue();

    std::cout << "=== G093 Overlay Dictionary Application ===\n";
    std::cout << "\n-- Worker effective values (shadow > shared) --\n";
    std::cout << "temperature: " << textValue(namedValue(root, "worker_temperature")) << "\n";
    std::cout << "max_tokens: " << textValue(namedValue(root, "worker_max_tokens")) << "\n";
    std::cout << "model: " << textValue(namedValue(root, "worker_model")) << "\n";
    std::cout << "system_prompt: " << textValue(namedValue(root, "worker_system_prompt")) << "\n";
    std::cout << "custom_instruction: " << textValue(namedValue(root, "worker_custom_instruction")) << "\n";

    std::cout << "\n-- Worker shadow keys (local diff) --\n";
    for (const auto& key : listStrings(namedValue(root, "worker_shadow_keys"))) {
        std::cout << "  " << key << " = " << textValue(namedValue(namedValue(root, "worker_diff"), key)) << "\n";
    }

    std::cout << "\n-- Coordinator frozen shared state (unchanged) --\n";
    std::cout << "temperature: " << textValue(namedValue(root, "coordinator_temperature")) << "\n";
    std::cout << "max_tokens: " << textValue(namedValue(root, "coordinator_max_tokens")) << "\n";

    std::cout << "\n=== Isolation proof ===\n";
    bool workerShadowed = textValue(namedValue(root, "worker_temperature")) == "0.2";
    bool coordinatorUnchanged = textValue(namedValue(root, "coordinator_temperature")) == "0.7";
    bool readOnlyPreserved = namedValue(root, "shared_config")->isReadOnly();

    std::cout << "Worker saw shadow value: " << (workerShadowed ? "YES" : "NO") << "\n";
    std::cout << "Coordinator unchanged: " << (coordinatorUnchanged ? "YES" : "NO") << "\n";
    std::cout << "ReadOnly preserved: " << (readOnlyPreserved ? "YES" : "NO") << "\n";

    if (workerShadowed && coordinatorUnchanged && readOnlyPreserved) {
        std::cout << "\nPASS: Overlay dictionary provides reference-scoped ReadOnly sharing.\n";
        return 0;
    } else {
        std::cout << "\nFAIL: Isolation or ReadOnly guarantee broken.\n";
        return 1;
    }
}
