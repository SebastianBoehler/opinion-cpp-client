#pragma once

#include "opinion/network.hpp"

#include <map>
#include <memory>
#include <string>

namespace opinion
{
    struct HttpResponse
    {
        long status_code{0};
        std::string body;
        std::string error;
        double elapsed_ms{0.0};
        std::map<std::string, std::string> headers;

        bool ok() const
        {
            return error.empty() && status_code >= 200 && status_code < 300;
        }
    };

    struct HttpClientOptions
    {
        long timeout_ms{15000};
        long connect_timeout_ms{5000};
        bool tcp_keepalive{true};
        std::string proxy_url;
        std::string interface_name;
        std::string user_agent{"opinion-cpp-client/0.1.0"};
    };

    // libcurl HTTP client. One instance is safe to use from one thread at a time;
    // requests take an internal mutex so overlapping calls on the same instance serialize.
    class HttpClient
    {
    public:
        HttpClient();
        explicit HttpClient(HttpClientOptions options);
        ~HttpClient();

        HttpClient(const HttpClient &) = delete;
        HttpClient &operator=(const HttpClient &) = delete;
        HttpClient(HttpClient &&) noexcept;
        HttpClient &operator=(HttpClient &&) noexcept;

        void configure(const HttpClientOptions &options);
        const HttpClientOptions &options() const;

        HttpResponse request(const std::string &method,
                             const std::string &url,
                             const std::string &body = {},
                             const std::map<std::string, std::string> &headers = {});

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
} // namespace opinion
