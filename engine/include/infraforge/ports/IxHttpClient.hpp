#pragma once

#include "infraforge/ports/HttpClient.hpp"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

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
        const std::string& url, const std::function<bool()>& cancelled) override {
        auto args = client_->createRequest(url, ix::HttpClient::kGet);
        args->connectTimeout = 15;
        args->transferTimeout = 60;
        args->followRedirects = true;
        args->maxRedirects = 5;

        // Check cancellation before the request.
        if (cancelled && cancelled()) {
            HttpResponse r;
            r.statusCode = 0;
            r.errorMessage = "cancelled before request";
            return r;
        }

        // Wire the canceller into the ixwebsocket progress callback so
        // that an in-flight transfer aborts promptly when cancellation
        // is requested (BLOCKER 2). ixwebsocket invokes the progress
        // callback during body transfer; returning false causes the
        // request to abort with HttpErrorCode::Cancelled.
        args->onProgressCallback = [&cancelled](int /*current*/, int /*total*/) -> bool {
            return !(cancelled && cancelled());
        };

        // The onProgressCallback only fires during body transfer. To
        // interrupt DNS, connect, TLS, and header-waiting phases, we
        // start a lightweight watcher thread that polls the canceller
        // every 50ms and sets args->cancel (the atomic that ixwebsocket
        // checks internally at every I/O boundary). The thread is joined
        // before this function returns, so it cannot outlive the
        // request or the caller's stack frame.
        std::atomic<bool> requestDone{false};
        std::thread watcher([&cancelled, args, &requestDone]() {
            while (!requestDone.load(std::memory_order_acquire)) {
                if (cancelled && cancelled()) {
                    args->cancel.store(true, std::memory_order_release);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });

        auto resp = client_->get(url, args);

        // Signal the watcher to stop and join it before returning.
        requestDone.store(true, std::memory_order_release);
        watcher.join();

        // If the request was cancelled mid-flight, report it clearly.
        if (cancelled && cancelled()) {
            HttpResponse r;
            r.statusCode = 0;
            r.errorMessage = "cancelled";
            return r;
        }
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
