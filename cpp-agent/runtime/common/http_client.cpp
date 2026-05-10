#include "http_client.h"

#include <array>
#include <iostream>

namespace agentc::runtime {

HttpClient::HttpClient() {
    curl_global_init(CURL_GLOBAL_ALL);
}

HttpClient::~HttpClient() {
    curl_global_cleanup();
}

size_t HttpClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* callback = static_cast<std::function<void(const char*, size_t)>*>(userdata);
    (*callback)(ptr, size * nmemb);
    return size * nmemb;
}

bool HttpClient::post_sse(const Request& req, std::function<void(const char*, size_t)> on_data) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    std::array<char, CURL_ERROR_SIZE> error_buffer{};

    struct curl_slist* headers = nullptr;
    for (const auto& h : req.headers) {
        headers = curl_slist_append(headers, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_data);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer.data());

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    if (res != CURLE_OK) {
        std::cerr << "[HttpClient] curl_easy_perform failed for " << req.url
                  << " (code=" << static_cast<int>(res)
                  << ", message=" << curl_easy_strerror(res);
        if (error_buffer[0] != '\0') {
            std::cerr << "; details=" << error_buffer.data();
        }
        std::cerr << ")" << std::endl;
    } else if (response_code >= 400) {
        std::cerr << "[HttpClient] HTTP error response for " << req.url
                  << ": status=" << response_code << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return res == CURLE_OK;
}

} // namespace agentc::runtime
