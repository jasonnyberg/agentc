#include "agent_root_vm_ops.h"

#include "agent_root_state.h"
#include "../../../edict/edict_compiler.h"
#include "../../../edict/edict_repl.h"
#include "../../../listree/listree.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace agentc::runtime {
namespace {

using json = nlohmann::json;

const agentc::edict::BytecodeBuffer& build_request_code() {
    static const agentc::edict::BytecodeBuffer code = agentc::edict::EdictCompiler().compile(R"(@prompt @agent_root {"system": "", "messages": [], "prompt": "", "response_mode": "text"} @request agent_root.conversation.system_prompt @request.system agent_root.conversation.messages @request.messages prompt @request.prompt [text] @request.response_mode request)");
    return code;
}

const agentc::edict::BytecodeBuffer& apply_response_code() {
    static const agentc::edict::BytecodeBuffer code = agentc::edict::EdictCompiler().compile(R"(@response @prompt @agent_root {"role": "user", "text": ""} @user_message prompt @user_message.text / user_message ^agent_root.conversation.messages^ prompt @agent_root.conversation.last_prompt / response @agent_root.conversation.last_response / response.message @assistant_message assistant_message.text @agent_root.conversation.assistant_text / assistant_message ^agent_root.conversation.messages^ prompt @agent_root.loop.last_prompt [turn-complete] @agent_root.loop.status)");
    return code;
}

const agentc::edict::BytecodeBuffer& apply_response_without_message_code() {
    static const agentc::edict::BytecodeBuffer code = agentc::edict::EdictCompiler().compile(R"(@response @prompt @agent_root {"role": "user", "text": ""} @user_message prompt @user_message.text / user_message ^agent_root.conversation.messages^ prompt @agent_root.conversation.last_prompt / response @agent_root.conversation.last_response / {"value": ""} @assistant_text_holder assistant_text_holder.value @agent_root.conversation.assistant_text / prompt @agent_root.loop.last_prompt [turn-complete] @agent_root.loop.status)");
    return code;
}

CPtr<agentc::ListreeValue> json_value_or_throw(const json& value, const char* label) {
    auto out = agentc::fromJson(value.dump());
    if (!out) {
        throw std::runtime_error(std::string("failed to materialize ") + label + " into Listree");
    }
    return out;
}

json json_from_value_or_throw(CPtr<agentc::ListreeValue> value, const char* label) {
    if (!value) {
        throw std::runtime_error(std::string("missing ") + label + " value");
    }
    return json::parse(agentc::toJson(value));
}

bool json_boolish(const json& value) {
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_string()) {
        return value.get<std::string>() == "true";
    }
    return false;
}

std::string read_text_file_or_throw(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.good()) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::filesystem::path executable_path_or_empty() {
    std::error_code ec;
    const auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : path;
}

std::filesystem::path first_existing_path_or_throw(std::initializer_list<std::filesystem::path> candidates,
                                                   const char* label) {
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error(std::string("failed to locate ") + label);
}

std::string shell_literal(const std::string& value) {
    return "[" + value + "]";
}

CPtr<agentc::ListreeValue> execute_vm_value_query_or_throw(
    agentc::edict::EdictVM& vm,
    const agentc::edict::BytecodeBuffer& code,
    std::initializer_list<CPtr<agentc::ListreeValue>> args,
    const char* label);

json merged_runtime_config_for_vm_agent_root(agentc::edict::EdictVM& vm,
                                             const json& base_runtime_config) {
    json config = base_runtime_config;
    const auto root = json_from_value_or_throw(vm.getCursor().getValue(), "embedded vm root");
    if (root.contains("runtime") && root["runtime"].is_object()) {
        for (auto it = root["runtime"].begin(); it != root["runtime"].end(); ++it) {
            config[it.key()] = it.value();
        }
    }
    return config;
}

std::string runtime_bootstrap_script_for_artifacts(const VmRuntimeImportArtifacts& artifacts) {
    std::ostringstream script;
    script << shell_literal(artifacts.extensions_library_path) << " "
           << shell_literal(artifacts.extensions_header_path) << " resolver.import ! @ext\n";
    script << shell_literal(artifacts.runtime_library_path) << " "
           << shell_literal(artifacts.runtime_header_path) << " resolver.import ! @runtimeffi\n";
    script << read_text_file_or_throw(artifacts.agentc_module_path) << "\n";
    return script.str();
}

json call_runtime_from_vm_or_throw(agentc::edict::EdictVM& vm,
                                   const json& runtime_config,
                                   const json& request,
                                   const VmRuntimeImportArtifacts& artifacts) {
    std::ostringstream script;
    script << runtime_bootstrap_script_for_artifacts(artifacts);
    script << shell_literal(runtime_config.dump()) << " from_json ! @vm_runtime_config\n";
    script << shell_literal(request.dump()) << " from_json ! @vm_runtime_request\n";
    script << "vm_runtime_config agentc_runtime_create ! @vm_runtime_handle\n";
    script << "vm_runtime_handle vm_runtime_request agentc_call ! @__vm_runtime_response\n";
    script << "vm_runtime_handle agentc_destroy ! /\n";

    std::stringstream input(script.str());
    std::stringstream output;
    agentc::edict::EdictREPL repl(vm.getCursor().getValue(), input, output);
    if (!repl.runScript(input)) {
        throw std::runtime_error("call_runtime_from_vm_or_throw failed inside embedded VM: " + output.str());
    }

    auto root = json_from_value_or_throw(vm.getCursor().getValue(), "embedded vm root after runtime call");
    if (!root.contains("__vm_runtime_response")) {
        throw std::runtime_error("call_runtime_from_vm_or_throw did not materialize __vm_runtime_response in the VM root");
    }

    const auto response = root["__vm_runtime_response"];
    root.erase("__vm_runtime_response");
    root.erase("vm_runtime_config");
    root.erase("vm_runtime_request");
    root.erase("vm_runtime_handle");
    vm.setCursor(json_value_or_throw(root, "embedded vm root after runtime cleanup"));
    vm.reset();
    return response;
}

CPtr<agentc::ListreeValue> execute_vm_value_query_or_throw(
    agentc::edict::EdictVM& vm,
    const agentc::edict::BytecodeBuffer& code,
    std::initializer_list<CPtr<agentc::ListreeValue>> args,
    const char* label) {
    for (const auto& arg : args) {
        vm.pushData(arg);
    }

    if (vm.execute(code) & agentc::edict::VM_ERROR) {
        const std::string error = vm.getError();
        vm.reset();
        throw std::runtime_error(std::string(label) + " failed inside embedded VM: " + error);
    }

    auto result = vm.popData();
    if (!result) {
        vm.reset();
        throw std::runtime_error(std::string(label) + " returned null value");
    }
    return result;
}

} // namespace

VmRuntimeImportArtifacts discover_vm_runtime_import_artifacts() {
    const auto cwd = std::filesystem::current_path();
    const auto exePath = executable_path_or_empty();
    const auto exeDir = exePath.empty() ? std::filesystem::path() : exePath.parent_path();
    const auto buildDir = exeDir.filename() == "cpp-agent" ? exeDir.parent_path() : exeDir;
    const auto sourceRoot = first_existing_path_or_throw(
        {cwd / "cpp-agent" / "edict" / "modules" / "agentc.edict",
         buildDir.parent_path() / "cpp-agent" / "edict" / "modules" / "agentc.edict"},
        "agentc.edict source root").parent_path().parent_path().parent_path().parent_path();

    VmRuntimeImportArtifacts artifacts;
    artifacts.extensions_library_path = first_existing_path_or_throw(
        {buildDir / "extensions" / "libagentc_extensions.so", cwd / "build" / "extensions" / "libagentc_extensions.so"},
        "agentc extensions library").string();
    artifacts.extensions_header_path = first_existing_path_or_throw(
        {sourceRoot / "extensions" / "agentc_stdlib.h"},
        "agentc extensions header").string();
    artifacts.runtime_library_path = first_existing_path_or_throw(
        {exeDir / "libagent_runtime.so", buildDir / "cpp-agent" / "libagent_runtime.so", cwd / "build" / "cpp-agent" / "libagent_runtime.so"},
        "agent runtime library").string();
    artifacts.runtime_header_path = first_existing_path_or_throw(
        {sourceRoot / "cpp-agent" / "include" / "agentc_runtime" / "agentc_runtime.h"},
        "agent runtime header").string();
    artifacts.agentc_module_path = first_existing_path_or_throw(
        {sourceRoot / "cpp-agent" / "edict" / "modules" / "agentc.edict"},
        "agentc.edict module").string();
    return artifacts;
}

json build_request_from_vm_agent_root(agentc::edict::EdictVM& vm,
                                      const std::string& prompt) {
    const auto requestValue = execute_vm_value_query_or_throw(
        vm,
        build_request_code(),
        {vm.getCursor().getValue(), agentc::createStringValue(prompt)},
        "build_request_from_vm_agent_root");
    const auto request = json_from_value_or_throw(requestValue, "request");
    vm.reset();
    return request;
}

void apply_runtime_response_to_vm_agent_root(agentc::edict::EdictVM& vm,
                                             const std::string& prompt,
                                             const json& response) {
    for (const auto& arg : {vm.getCursor().getValue(), agentc::createStringValue(prompt), json_value_or_throw(response, "runtime response")}) {
        vm.pushData(arg);
    }

    const auto& code = (response.is_object() && response.contains("message") && response["message"].is_object())
        ? apply_response_code()
        : apply_response_without_message_code();

    if (vm.execute(code) & agentc::edict::VM_ERROR) {
        const std::string error = vm.getError();
        vm.reset();
        throw std::runtime_error(std::string("apply_runtime_response_to_vm_agent_root failed inside embedded VM: ") + error);
    }

    const auto normalizedRoot = normalize_agent_root(
        json_from_value_or_throw(vm.getCursor().getValue(), "embedded vm root"),
        "",
        "google",
        "gemini-2.5-flash");
    vm.setCursor(json_value_or_throw(normalizedRoot, "normalized embedded vm root"));
    vm.reset();
}

json run_vm_agent_root_turn(agentc::edict::EdictVM& vm,
                            const std::string& prompt,
                            const VmAgentRuntimeInvoker& runtime_invoker) {
    const auto request = build_request_from_vm_agent_root(vm, prompt);
    const auto response = runtime_invoker(request);
    apply_runtime_response_to_vm_agent_root(vm, prompt, response);
    return response;
}

json run_vm_agent_root_turn_via_imported_runtime(agentc::edict::EdictVM& vm,
                                                 const std::string& prompt,
                                                 const json& base_runtime_config,
                                                 const VmRuntimeImportArtifacts& artifacts) {
    const auto request = build_request_from_vm_agent_root(vm, prompt);
    const auto runtime_config = merged_runtime_config_for_vm_agent_root(vm, base_runtime_config);
    const auto response = call_runtime_from_vm_or_throw(vm, runtime_config, request, artifacts);
    apply_runtime_response_to_vm_agent_root(vm, prompt, response);
    return response;
}

std::string reply_text_from_vm_agent_root(agentc::edict::EdictVM& vm) {
    const auto root = json_from_value_or_throw(vm.getCursor().getValue(), "embedded vm root");
    if (!root.contains("conversation") || !root["conversation"].is_object()) {
        return "[no assistant text returned]";
    }

    const auto& conversation = root["conversation"];
    if (conversation.contains("assistant_text") && conversation["assistant_text"].is_string()) {
        const auto text = conversation["assistant_text"].get<std::string>();
        if (!text.empty()) {
            return text;
        }
    }

    if (conversation.contains("last_response") && conversation["last_response"].is_object()) {
        const auto& response = conversation["last_response"];
        if (response.contains("ok") && !json_boolish(response["ok"])) {
            if (response.contains("error") && response["error"].is_object()) {
                return std::string("[error] ") + response["error"].value("message", std::string("unknown runtime error"));
            }
            return "[error] request failed";
        }
    }

    return "[no assistant text returned]";
}

} // namespace agentc::runtime
