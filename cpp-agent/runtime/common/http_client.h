#pragma once

#include <curl/curl.h>

#include <functional>
#include <string>
#include <vector>

namespace agentc::runtime {

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    struct Request {
        std::string url;
        std::string body;
        std::vector<std::string> headers;
    };

    bool post_sse(const Request& req, std::function<void(const char*, size_t)> on_data);

private:
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};

} // namespace agentc::runtime
