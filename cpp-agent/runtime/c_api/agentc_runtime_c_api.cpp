#include "../../include/agentc_runtime/agentc_runtime.h"
#include "../core/runtime.h"

#include <cstring>
#include <exception>
#include <new>
#include <string>

namespace {

char* dup_cstr(const std::string& value) {
    char* out = new (std::nothrow) char[value.size() + 1];
    if (!out) {
        return nullptr;
    }
    std::memcpy(out, value.data(), value.size());
    out[value.size()] = '\0';
    return out;
}

agentc::runtime::Runtime* cast_runtime(agentc_runtime_t runtime) {
    return static_cast<agentc::runtime::Runtime*>(runtime);
}

} // namespace

extern "C" const char* agentc_runtime_version(void) {
    return "0.1.0";
}

extern "C" agentc_runtime_t agentc_runtime_create_json(const char* config_json) {
    try {
        return new agentc::runtime::Runtime(config_json ? std::string(config_json) : std::string("{}"));
    } catch (...) {
        return nullptr;
    }
}

extern "C" agentc_runtime_t agentc_runtime_create_file(const char* config_path) {
    try {
        auto* runtime = new agentc::runtime::Runtime();
        runtime->configure_file(config_path ? std::string(config_path) : std::string());
        return runtime;
    } catch (...) {
        return nullptr;
    }
}

extern "C" int agentc_runtime_configure_json(agentc_runtime_t runtime, const char* config_json) {
    auto* impl = cast_runtime(runtime);
    if (!impl) return 1;
    try {
        impl->configure_json(config_json ? std::string(config_json) : std::string("{}"));
        return 0;
    } catch (...) {
        return 2;
    }
}

extern "C" int agentc_runtime_configure_file(agentc_runtime_t runtime, const char* config_path) {
    auto* impl = cast_runtime(runtime);
    if (!impl) return 1;
    try {
        impl->configure_file(config_path ? std::string(config_path) : std::string());
        return 0;
    } catch (...) {
        return 2;
    }
}

extern "C" char* agentc_runtime_request_json(agentc_runtime_t runtime, const char* request_json) {
    auto* impl = cast_runtime(runtime);
    if (!impl) {
        return nullptr;
    }
    try {
        const auto response = impl->request_json(request_json ? std::string(request_json) : std::string("{}"));
        return dup_cstr(response.dump());
    } catch (...) {
        return nullptr;
    }
}

extern "C" char* agentc_runtime_last_error_json(agentc_runtime_t runtime) {
    auto* impl = cast_runtime(runtime);
    if (!impl) return nullptr;
    const std::string value = impl->last_error_json();
    return value.empty() ? nullptr : dup_cstr(value);
}

extern "C" char* agentc_runtime_last_trace_json(agentc_runtime_t runtime) {
    auto* impl = cast_runtime(runtime);
    if (!impl) return nullptr;
    const std::string value = impl->last_trace_json();
    return value.empty() ? nullptr : dup_cstr(value);
}

extern "C" void agentc_runtime_destroy(agentc_runtime_t runtime) {
    delete cast_runtime(runtime);
}

extern "C" void agentc_runtime_free_string(char* value) {
    delete[] value;
}
