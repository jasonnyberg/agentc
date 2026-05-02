#include "../../include/agentc_runtime/agentc_runtime.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

struct MockRuntime {
    json config = json::object();
    json last_error = nullptr;
    json last_trace = nullptr;
};

char* dup_cstr(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (!out) {
        return nullptr;
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

json error_response(MockRuntime* runtime, const std::string& code, const std::string& message) {
    runtime->last_error = json{{"code", code}, {"message", message}, {"retryable", false}};
    return json{
        {"ok", false},
        {"message", nullptr},
        {"error", runtime->last_error}
    };
}

} // namespace

extern "C" {

const char* agentc_runtime_version(void) {
    return "mock-1.0";
}

void* agentc_runtime_create_json(const char* config_json) {
    try {
        auto* runtime = new MockRuntime();
        runtime->config = json::parse(config_json ? config_json : "{}");
        runtime->last_trace = json{{"configured", true}};
        return runtime;
    } catch (...) {
        return nullptr;
    }
}

void* agentc_runtime_create_file(const char* config_path) {
    (void)config_path;
    return agentc_runtime_create_json("{}");
}

int agentc_runtime_configure_json(void* runtime_ptr, const char* config_json) {
    auto* runtime = static_cast<MockRuntime*>(runtime_ptr);
    if (!runtime) {
        return 1;
    }
    try {
        runtime->config = json::parse(config_json ? config_json : "{}");
        runtime->last_trace = json{{"configured", true}};
        runtime->last_error = nullptr;
        return 0;
    } catch (...) {
        runtime->last_error = json{{"code", "config_invalid"}, {"message", "invalid config"}, {"retryable", false}};
        return 2;
    }
}

int agentc_runtime_configure_file(void* runtime_ptr, const char* config_path) {
    (void)config_path;
    return agentc_runtime_configure_json(runtime_ptr, "{}");
}

char* agentc_runtime_request_json(void* runtime_ptr, const char* request_json) {
    auto* runtime = static_cast<MockRuntime*>(runtime_ptr);
    if (!runtime) {
        return nullptr;
    }

    try {
        const auto request = json::parse(request_json ? request_json : "{}");
        runtime->last_trace = json{{"provider", runtime->config.value("default_provider", "mock")},
                                   {"model", runtime->config.value("default_model", "mock-model")}};

        if ((!request.contains("prompt") || !request["prompt"].is_string()) &&
            (!request.contains("messages") || !request["messages"].is_array())) {
            return dup_cstr(error_response(runtime, "request_invalid", "Request must provide prompt or messages").dump());
        }

        runtime->last_error = nullptr;
        const std::string prompt = request.value("prompt", std::string());
        const json response = {
            {"ok", true},
            {"provider", runtime->config.value("default_provider", "mock")},
            {"model", runtime->config.value("default_model", "mock-model")},
            {"message", {
                {"role", "assistant"},
                {"text", "mock:" + prompt}
            }},
            {"error", nullptr},
            {"trace", runtime->last_trace}
        };
        return dup_cstr(response.dump());
    } catch (...) {
        return dup_cstr(error_response(runtime, "internal_error", "mock runtime failed").dump());
    }
}

char* agentc_runtime_last_error_json(void* runtime_ptr) {
    auto* runtime = static_cast<MockRuntime*>(runtime_ptr);
    if (!runtime || runtime->last_error.is_null()) {
        return nullptr;
    }
    return dup_cstr(runtime->last_error.dump());
}

char* agentc_runtime_last_trace_json(void* runtime_ptr) {
    auto* runtime = static_cast<MockRuntime*>(runtime_ptr);
    if (!runtime || runtime->last_trace.is_null()) {
        return nullptr;
    }
    return dup_cstr(runtime->last_trace.dump());
}

void agentc_runtime_destroy(void* runtime_ptr) {
    delete static_cast<MockRuntime*>(runtime_ptr);
}

void agentc_runtime_free_string(char* value) {
    std::free(value);
}

} // extern "C"
