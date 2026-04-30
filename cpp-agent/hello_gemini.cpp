#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Callback for curl to store response
size_t write_data(void* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

int main() {
    char* api_key = std::getenv("GEMINI_API_KEY");
    if (!api_key) {
        std::cerr << "Error: GEMINI_API_KEY not set." << std::endl;
        return 1;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    CURL* curl = curl_easy_init();
    if (!curl) return 1;

    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.1-flash-lite-preview:generateContent?key=" + std::string(api_key);

    json payload = {
        {"contents", {{{"parts", {{{"text", "Print Hello world!"}}}}}}}
    };
    std::string body = payload.dump();

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    std::cout << "Requesting: " << url << std::endl;
    CURLcode res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        std::cout << "\n--- Raw JSON Response ---\n" << response << std::endl;
        try {
            auto j = json::parse(response);
            std::cout << "\n--- Gemini Response ---\n" 
                      << j["candidates"][0]["content"]["parts"][0]["text"] 
                      << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "JSON Parse error: " << e.what() << "\nResponse: " << response << std::endl;
        }
    } else {
        std::cerr << "Curl Error: " << curl_easy_strerror(res) << std::endl;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    return 0;
}
