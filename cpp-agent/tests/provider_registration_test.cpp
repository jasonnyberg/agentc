#include <gtest/gtest.h>
#include "../api_registry.h"
#include "../providers/google.h"
#include "../providers/openai.h"

TEST(ProviderRegistrationTest, AllProvidersRegister) {
    register_google_provider();
    register_openai_provider();

    EXPECT_NO_THROW(get_provider("google-gemini-cli"));
    EXPECT_NO_THROW(get_provider("openai-completions"));
}
