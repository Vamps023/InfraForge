#pragma once

#include "infraforge/ports/HttpClient.hpp"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <atomic>
#include <memory>

namespace infraforge::ports {

// Production HTTP client backed by ixwebsocket's HttpClient.
class IxHttpClient final : public HttpClient {
public:
    IxHttpClient() : client_(std::make_unique<ix::HttpClient>()) {
        // Disable TLS verification override (use system defaults).
        ix::SocketTLSOptions tlsOptions;
        client_->setTLSOptions(tlsOptions);
    }

    ~IxHttpClient() override = default;

    [[nodiscard]] HttpResponse get(const std::string& url) override {
        auto args = client_->createRequest(url, ix::HttpClient::kGet);
        args->connectTimeout = 15;
        args->transferTimeout = 60;
        args->followRedirects = true;
        args->maxRedirects = 5;
        auto resp = client_->get(url, args);
        return convertResponse(resp);
    }

    [[nodiscard]] HttpResponse get(
        const std::string& url, const std::atomic<bool>& cancelled) override {
        auto args = client_->createRequest(url, ix::HttpClient::kGet);
        args->connectTimeout = 15;
        args->transferTimeout = 60;
        args->followRedirects = true;
        args->maxRedirects = 5;
        // Check cancellation before the request.
        if (cancelled.load()) {
            HttpResponse r;
            r.statusCode = 0;
            r.errorMessage = "cancelled before request";
            return r;
        }
        auto resp = client_->get(url, args);
        return convertResponse(resp);
    }

private:
    [[nodiscard]] HttpResponse convertResponse(const ix::HttpResponsePtr& resp) {
        HttpResponse r;
        if (!resp) {
            r.statusCode = 0;
            r.errorMessage = "no response from HTTP client";
            return r;
        }
        r.statusCode = resp->statusCode;
        r.body = resp->body;
        if (!resp->errorMsg.empty()) {
            r.errorMessage = resp->errorMsg;
        } else if (resp->errorCode != ix::HttpErrorCode::Ok) {
            r.errorMessage = "HTTP error code: " + std::to_string(
                static_cast<int>(resp->errorCode));
        }
        return r;
    }

    std::unique_ptr<ix::HttpClient> client_;
};

} // namespace infraforge::ports
