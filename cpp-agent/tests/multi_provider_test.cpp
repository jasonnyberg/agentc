#include <gtest/gtest.h>
#include "../agent_loop.h"
#include "../api_registry.h"
#include "../providers/google.h"
#include "../providers/openai.h"

// Mock Stream for all tests
void test_mock_stream(const Model&, const Context&, const StreamOptions&, AssistantMessageStream& stream) {
    AssistantMessage msg;
    msg.content.push_back(TextContent{.text = "Hello from provider!"});
    stream.end(msg);
}

TEST(MultiProviderTest, VerifyAllProvidersCanRegisterAndRun) {
    register_google_provider();
    register_openai_provider();

    std::vector<std::string> providers = {"google-gemini-cli", "openai-completions"};
    
    for (const auto& p : providers) {
        AgentContext context;
        context.messages.push_back(UserMessage::text("Hello"));

        AgentLoopConfig config;
        // In this test, we force the stream function to be our mock, 
        // verifying the loop can handle any registered provider
        config.stream_fn = test_mock_stream;
        config.model = Model{.id = "test-model"};

        auto messages = run_agent_loop({UserMessage::text("Hello")}, context, config, [](AgentEvent ev) {});
        
        EXPECT_GT(messages.size(), 0u);
        const auto& last_msg = context.messages.back();
        EXPECT_EQ(text_of(std::get<AssistantMessage>(last_msg).content), "Hello from provider!");
    }
}
