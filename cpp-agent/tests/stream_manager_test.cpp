#include <gtest/gtest.h>
#include "runtime/core/stream_manager.h"
#include <thread>
#include <vector>

using namespace agentc::runtime;

TEST(StreamManagerTest, BasicPushAndDrain) {
    StreamManager sm;
    std::string sid = sm.createStream();

    sm.pushToken(sid, "Hello");
    sm.pushToken(sid, " World");
    
    auto [text, complete] = sm.syncStream(sid);
    EXPECT_EQ(text, "Hello World");
    EXPECT_FALSE(complete);

    sm.pushToken(sid, "!");
    sm.markComplete(sid);

    auto [text2, complete2] = sm.syncStream(sid);
    EXPECT_EQ(text2, "!");
    EXPECT_TRUE(complete2);

    // Should be cleaned up
    auto [text3, complete3] = sm.syncStream(sid);
    EXPECT_EQ(text3, "");
    EXPECT_TRUE(complete3);
}

TEST(StreamManagerTest, ConcurrentAccess) {
    StreamManager sm;
    std::string sid = sm.createStream();

    std::thread producer([&sm, sid]() {
        for (int i = 0; i < 100; ++i) {
            sm.pushToken(sid, "x");
            std::this_thread::yield();
        }
        sm.markComplete(sid);
    });

    std::string accumulated;
    bool done = false;

    while (!done) {
        auto [chunk, complete] = sm.syncStream(sid);
        accumulated += chunk;
        done = complete;
    }

    producer.join();

    EXPECT_EQ(accumulated.length(), 100u);
}
