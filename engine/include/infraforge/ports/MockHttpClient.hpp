#pragma once

#include "infraforge/ports/HttpClient.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace infraforge::ports {

// Mock HTTP client for deterministic testing. Returns pre-configured
// responses for specific URLs, or reads local files as response bodies.
// Never performs real network I/O.
class MockHttpClient final : public HttpClient {
public:
    // Set a fixed response body for a specific URL.
    void setResponse(const std::string& url, int statusCode, const std::string& body) {
        responses_[url] = {statusCode, body, ""};
    }

    // Set a fixed response body from a file for a specific URL.
    void setResponseFromFile(const std::string& url, const std::filesystem::path& file) {
        std::ifstream in(file, std::ios::binary);
        std::string body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        responses_[url] = {200, body, ""};
    }

    // Set an error response for a specific URL.
    void setErrorResponse(const std::string& url, int statusCode, const std::string& errorMsg) {
        responses_[url] = {statusCode, "", errorMsg};
    }

    [[nodiscard]] HttpResponse get(const std::string& url) override {
        requested_.push_back(url);
        auto it = responses_.find(url);
        if (it != responses_.end()) {
            return it->second;
        }
        HttpResponse r;
        r.statusCode = 404;
        r.errorMessage = "mock: no response configured for " + url;
        return r;
    }

    [[nodiscard]] HttpResponse get(
        const std::string& url, const std::atomic<bool>& cancelled) override {
        if (cancelled.load()) {
            HttpResponse r;
            r.statusCode = 0;
            r.errorMessage = "cancelled";
            return r;
        }
        return get(url);
    }

    // Track which URLs were requested (for test assertions).
    [[nodiscard]] const std::vector<std::string>& requestedUrls() const noexcept {
        return requested_;
    }

private:
    std::map<std::string, HttpResponse> responses_;
    std::vector<std::string> requested_;
};

} // namespace infraforge::ports
