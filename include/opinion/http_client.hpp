#pragma once

#include <map>
#include <mutex>
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

        std::string header(const std::string &name) const;
    };

    struct HttpClientOptions
    {
        long timeout_ms{10000};
        long connect_timeout_ms{5000};
        bool tcp_keepalive{true};
        std::string proxy_url;
        std::string user_agent;
    };

    struct RequestMetrics
    {
        std::string method;
        std::string url;
        long status_code{0};
        double elapsed_ms{0.0};
        bool reused_connection{false};
    };

    // libcurl easy handle with connection reuse. Not safe to share across
    // threads without external synchronization; each call takes an internal mutex.
    class HttpClient
    {
    public:
        HttpClient();
        explicit HttpClient(HttpClientOptions options);
        ~HttpClient();

        HttpClient(const HttpClient &) = delete;
        HttpClient &operator=(const HttpClient &) = delete;
        HttpClient(HttpClient &&other) noexcept;
        HttpClient &operator=(HttpClient &&other) noexcept;

        void set_base_url(const std::string &base_url);
        void set_proxy(const std::string &proxy_url);
        void set_header(const std::string &name, const std::string &value);
        void clear_header(const std::string &name);

        const std::string &base_url() const { return base_url_; }
        HttpClientOptions options() const;

        HttpResponse get(const std::string &path);
        HttpResponse post(const std::string &path, const std::string &body);
        HttpResponse del(const std::string &path, const std::string &body = {});

        RequestMetrics last_metrics() const;

    private:
        struct State;
        State *state_{nullptr};
        std::string base_url_;
        HttpClientOptions options_;
        std::map<std::string, std::string> headers_;
        mutable std::mutex mutex_;
        RequestMetrics last_{};
        bool owns_global_{false};

        HttpResponse perform(const std::string &method, const std::string &path, const std::string &body);
        void apply_options();
    };

    void http_global_init();
    void http_global_cleanup();
} // namespace opinion
