#pragma once
#include <string>
#include <vector>
#include <functional>
#include <curl/curl.h>

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    struct Request {
        std::string url;
        std::string body;
        std::vector<std::string> headers;
    };

    // Performs POST request with SSE streaming, calling on_data for each chunk
    bool post_sse(const Request& req, std::function<void(const char*, size_t)> on_data);

private:
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
};
