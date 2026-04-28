#include <gtest/gtest.h>
#include "../agent_loop.h"
#include "../agent_types.h"

void mock_stream(const Model&, const Context&, const StreamOptions&, AssistantMessageStream& stream) {
    AssistantMessage msg;
    msg.content.push_back(TextContent{.text = "Test response"});
    stream.end(msg);
}

TEST(AgentLoopTest, MockProviderCompletesTurn) {
    AgentContext context;
    context.messages.push_back(UserMessage::text("Hello"));

    AgentLoopConfig config;
    config.stream_fn = mock_stream;
    config.model = Model{.id = "test-model"};

    bool started = false;
    bool ended = false;

    auto messages = run_agent_loop({UserMessage::text("Hello")}, context, config, [&](AgentEvent ev) {
        if (std::get_if<EvAgentStart>(&ev)) started = true;
        if (std::get_if<EvAgentEnd>(&ev)) ended = true;
    });

    EXPECT_TRUE(started);
    EXPECT_TRUE(ended);
    EXPECT_GT(messages.size(), 0u);
}
