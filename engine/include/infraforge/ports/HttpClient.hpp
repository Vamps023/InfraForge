#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace infraforge::ports {

// HTTP response from a GET request.
struct HttpResponse {
    int statusCode{0};
    std::string body;
    std::string errorMessage;
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

// Abstract HTTP client port. The production implementation uses
// ixwebsocket's HttpClient; tests use a mock that returns local files.
class HttpClient {
public:
    virtual ~HttpClient() = default;

    // Synchronous GET request. Returns the response body and status.
    // The implementation must:
    //   - follow redirects (up to a bounded count)
    //   - set a reasonable timeout
    //   - return a typed error on failure (timeout, DNS, connection)
    [[nodiscard]] virtual HttpResponse get(const std::string& url) = 0;

    // Synchronous GET with cancellation. The canceller is polled during
    // the transfer via the ixwebsocket progress callback; returning true
    // aborts the in-flight request promptly (BLOCKER 2).
    [[nodiscard]] virtual HttpResponse get(
        const std::string& url, const std::function<bool()>& cancelled) = 0;
};

} // namespace infraforge::ports
