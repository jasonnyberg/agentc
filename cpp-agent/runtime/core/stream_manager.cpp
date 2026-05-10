#include "stream_manager.h"
#include <stdexcept>

namespace agentc::runtime {

std::string StreamManager::createStream() {
    std::lock_guard<std::mutex> lock(global_mtx);
    std::string id = "stream_" + std::to_string(next_id++);
    streams[id] = std::make_shared<StreamState>();
    return id;
}

void StreamManager::pushToken(const std::string& stream_id, const std::string& token) {
    std::shared_ptr<StreamState> state;
    {
        std::lock_guard<std::mutex> lock(global_mtx);
        auto it = streams.find(stream_id);
        if (it != streams.end()) {
            state = it->second;
        }
    }

    if (state) {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->tokens.push(token);
    }
}

void StreamManager::markComplete(const std::string& stream_id) {
    std::shared_ptr<StreamState> state;
    {
        std::lock_guard<std::mutex> lock(global_mtx);
        auto it = streams.find(stream_id);
        if (it != streams.end()) {
            state = it->second;
        }
    }

    if (state) {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->is_complete = true;
    }
}

void StreamManager::markError(const std::string& stream_id, const std::string& error_msg) {
    std::shared_ptr<StreamState> state;
    {
        std::lock_guard<std::mutex> lock(global_mtx);
        auto it = streams.find(stream_id);
        if (it != streams.end()) {
            state = it->second;
        }
    }

    if (state) {
        std::lock_guard<std::mutex> lock(state->mtx);
        state->error = error_msg;
        state->is_complete = true;
    }
}

std::pair<std::string, bool> StreamManager::syncStream(const std::string& stream_id) {
    std::shared_ptr<StreamState> state;
    {
        std::lock_guard<std::mutex> lock(global_mtx);
        auto it = streams.find(stream_id);
        if (it != streams.end()) {
            state = it->second;
        } else {
            // Stream was cleaned up or never existed
            return {"", true}; 
        }
    }

    std::string accumulated;
    bool is_complete = false;
    std::string err;

    {
        std::lock_guard<std::mutex> lock(state->mtx);
        while (!state->tokens.empty()) {
            accumulated += state->tokens.front();
            state->tokens.pop();
        }
        is_complete = state->is_complete;
        err = state->error;
    }

    if (!err.empty()) {
        throw std::runtime_error("Stream error: " + err);
    }

    // Auto-cleanup if complete
    if (is_complete) {
        removeStream(stream_id);
    }

    return {accumulated, is_complete};
}

void StreamManager::removeStream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(global_mtx);
    streams.erase(stream_id);
}

} // namespace agentc::runtime
