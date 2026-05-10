// This file is part of AgentC.
#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <optional>

namespace agentc::runtime {

struct StreamState {
    std::queue<std::string> tokens;
    bool is_complete = false;
    std::string error;
    std::mutex mtx;
};

class StreamManager {
public:
    StreamManager() = default;
    ~StreamManager() = default;

    // Disallow copy/move
    StreamManager(const StreamManager&) = delete;
    StreamManager& operator=(const StreamManager&) = delete;

    // Register a new stream and get its ID
    std::string createStream();

    // Push a token to an existing stream (Called by background thread)
    void pushToken(const std::string& stream_id, const std::string& token);

    // Mark a stream as complete (Called by background thread)
    void markComplete(const std::string& stream_id);

    // Mark a stream as failed (Called by background thread)
    void markError(const std::string& stream_id, const std::string& error_msg);

    // Drain all pending tokens for a stream (Called by main thread/Edict)
    // Returns a pair of (tokens_concatenated, is_complete)
    // If an error occurred, throws runtime_error to Edict
    std::pair<std::string, bool> syncStream(const std::string& stream_id);

    // Clean up a stream explicitly
    void removeStream(const std::string& stream_id);

private:
    std::mutex global_mtx;
    std::unordered_map<std::string, std::shared_ptr<StreamState>> streams;
    uint64_t next_id = 1;
};

} // namespace agentc::runtime
