#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace infraforge::ports {

// Transport-level error category for an HTTP request. Preserves the
// underlying transport failure type so the terrain domain can map it to
// the correct provider error (e.g. DNS failure vs timeout vs cancellation)
// instead of collapsing all transport failures to "timeout".
enum class TransportError {
    None,                 // No transport error (HTTP exchange completed).
    Cancelled,            // Request was cancelled by the caller.
    Timeout,              // Connect or transfer timeout.
    DnsFailure,           // DNS resolution failure.
    ConnectionFailure,   // TCP connect / connection reset.
    TlsFailure,           // TLS handshake / certificate failure.
    ProtocolFailure,      // HTTP protocol error (malformed response).
    UnknownNetworkFailure, // Network failure that doesn't fit above.
};

// HTTP response from a GET request.
struct HttpResponse {
    int statusCode{0};
    std::string body;
    std::string errorMessage;
    TransportError transportError{TransportError::None};
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
    //   - preserve the transport error category in HttpResponse
    [[nodiscard]] virtual HttpResponse get(const std::string& url) = 0;

    // Synchronous GET with cancellation. The implementation must support
    // prompt cancellation throughout the synchronous request lifecycle
    // where the transport library supports it — including DNS, connect,
    // TLS, send, header wait, and body transfer phases. The canceller
    // returns true if cancellation was requested; the implementation
    // aborts the in-flight request and returns a response with
    // TransportError::Cancelled (BLOCKER 2, Finding 11).
    [[nodiscard]] virtual HttpResponse get(
        const std::string& url, const std::function<bool()>& cancelled) = 0;
};

} // namespace infraforge::ports
