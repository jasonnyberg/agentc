#include <gtest/gtest.h>
#include <iostream>
#include "../api_registry.h"
#include "../providers/google.h"
#include "../providers/openai.h"
#include "../credentials.h"

TEST(ProviderIntegrationDemo, DemonstrateConfiguration) {
    register_google_provider();
    register_openai_provider();

    std::vector<std::string> provider_names = {"google-gemini-cli", "openai-completions"};
    
    std::cout << "\n--- Provider Configuration Audit ---" << std::endl;
    for (const auto& name : provider_names) {
        std::cout << "Testing Provider: " << name << std::endl;
        try {
            auto stream_fn = get_provider(name);
            std::cout << "  - Provider registered successfully." << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  - Error: " << e.what() << std::endl;
        }
    }
}
