#include "register_builtin_providers.h"

#include "google/google_provider.h"
#include "openai/openai_provider.h"

#include <mutex>

namespace agentc::runtime {

void register_builtin_providers_once() {
    static std::once_flag once;
    std::call_once(once, []() {
        register_google_provider();
        register_openai_provider();
    });
}

} // namespace agentc::runtime
